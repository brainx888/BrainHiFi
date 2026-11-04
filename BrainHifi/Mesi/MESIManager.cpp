#include "MESIManager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace mesi {

namespace {

std::string joinPath(const std::string& dir, const char* file)
{
	if (dir.empty()) return file;
	const char last = dir.back();
	return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
}

std::string joinErrors(const std::vector<std::string>& errors)
{
	std::string out;
	for (const auto& e : errors) { if (!out.empty()) out += ' '; out += e; }
	return out;
}

constexpr const char* kSubjectsFile = "subjects.json";
constexpr const char* kResultsFile  = "results.json";
constexpr const char* kConfigFile   = "ai_config.json";

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
MESIManager::MESIManager(Options options)
	: m_options(std::move(options))
	, m_jobs(m_options.workerThreads)
{
	m_jobs.setObserver([this](const JobInfo& info) { onJobUpdate(info); });
	m_cohort.setChangeListener([this](SubjectCohortManager::ChangeKind kind, const std::string& id) { onCohortChange(kind, id); });
}

MESIManager::~MESIManager()
{
	// Detach observers first so no event reaches a half-destroyed sink.
	setEventSink(nullptr);
	m_jobs.setObserver(nullptr);
	m_cohort.setChangeListener(nullptr);
	m_jobs.cancelAll();
	// MesiJobRunner's destructor joins the workers.
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
void MESIManager::setEventSink(EventSink sink)
{
	std::unique_lock lock(m_sinkMutex);
	m_sink = std::move(sink);
}

void MESIManager::emit(const MesiEvent& ev) const
{
	EventSink sink;
	{
		std::shared_lock lock(m_sinkMutex);
		sink = m_sink;
	}
	if (sink) sink(ev);
}

void MESIManager::onCohortChange(SubjectCohortManager::ChangeKind, const std::string& id)
{
	MesiEvent ev;
	ev.type = MesiEvent::Type::SubjectsChanged;
	ev.subjectId = id;
	emit(ev);
}

void MESIManager::onJobUpdate(const JobInfo& info)
{
	MesiEvent ev;
	ev.jobId = info.id;
	ev.kind = info.kind;
	ev.subjectId = info.subjectId;
	ev.percent = info.percent;
	ev.message = info.stage;
	switch (info.status) {
	case JobStatus::Queued:    ev.type = MesiEvent::Type::JobQueued; break;
	case JobStatus::Running:   ev.type = info.percent == 0 && info.stage == "Starting" ? MesiEvent::Type::JobStarted : MesiEvent::Type::JobProgress; break;
	case JobStatus::Completed: ev.type = MesiEvent::Type::JobCompleted; break;
	case JobStatus::Failed:    ev.type = MesiEvent::Type::JobFailed; ev.message = info.error; break;
	case JobStatus::Cancelled: ev.type = MesiEvent::Type::JobCancelled; break;
	}

	// Mirror the terminal job status into the subject record.
	if (info.status == JobStatus::Failed)
		m_cohort.setState(info.subjectId, ProcessingState::Failed, info.error);
	else if (info.status == JobStatus::Cancelled)
		m_cohort.setState(info.subjectId, ProcessingState::Cancelled);
	emit(ev);
}

// ---------------------------------------------------------------------------
// Configuration / engines
// ---------------------------------------------------------------------------
AISegmentationConfig MESIManager::aiConfig() const
{
	std::shared_lock lock(m_configMutex);
	return m_aiConfig;
}

std::vector<std::string> MESIManager::setAiConfig(const AISegmentationConfig& cfg)
{
	auto errors = cfg.validate();
	if (!errors.empty()) return errors;
	{
		std::unique_lock lock(m_configMutex);
		m_aiConfig = cfg;
	}
	MesiEvent ev;
	ev.type = MesiEvent::Type::ConfigChanged;
	ev.message = cfg.displayName();
	emit(ev);
	return {};
}

void MESIManager::setSegmentationEngine(std::shared_ptr<IAISegmentationEngine> engine)
{
	std::unique_lock lock(m_configMutex);
	m_segEngine = std::move(engine);
}
void MESIManager::setFusionEngine(std::shared_ptr<IMESIFusionEngine> engine)
{
	std::unique_lock lock(m_configMutex);
	m_fusionEngine = std::move(engine);
}
std::shared_ptr<IAISegmentationEngine> MESIManager::segmentationEngine() const
{
	std::shared_lock lock(m_configMutex);
	return m_segEngine;
}
std::shared_ptr<IMESIFusionEngine> MESIManager::fusionEngine() const
{
	std::shared_lock lock(m_configMutex);
	return m_fusionEngine;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------
bool MESIManager::prepareJob(const std::string& subjectId, bool needSegEngine, bool needFusionEngine,
	SubjectRecord& subjectOut, std::string* errorOut) const
{
	const auto subject = m_cohort.find(subjectId);
	if (!subject) { if (errorOut) *errorOut = "Unknown subject '" + subjectId + "'."; return false; }
	if (m_jobs.isSubjectBusy(subjectId)) { if (errorOut) *errorOut = "Subject '" + subjectId + "' already has a job queued or running."; return false; }
	if (const auto errs = subject->modality.validate(); !errs.empty()) {
		if (errorOut) *errorOut = "Modality configuration incomplete: " + joinErrors(errs);
		return false;
	}
	{
		std::shared_lock lock(m_configMutex);
		if (needSegEngine && !m_segEngine) { if (errorOut) *errorOut = "No AI segmentation engine installed."; return false; }
		if (needFusionEngine && !m_fusionEngine) { if (errorOut) *errorOut = "No MESI fusion engine installed."; return false; }
		if (needSegEngine) {
			if (const auto errs = m_aiConfig.validate(); !errs.empty()) {
				if (errorOut) *errorOut = "AI configuration invalid: " + joinErrors(errs);
				return false;
			}
			if (!m_segEngine->supports(m_aiConfig.runtime)) {
				if (errorOut) *errorOut = "Engine '" + m_segEngine->name() + "' does not support runtime " + toString(m_aiConfig.runtime) + ".";
				return false;
			}
		}
	}
	subjectOut = *subject;
	return true;
}

SegmentationOutput MESIManager::doSegmentation(const SubjectRecord& subject, const CancellationToken& cancel,
	const ProgressFn& progress, int progressFrom, int progressTo)
{
	std::shared_ptr<IAISegmentationEngine> engine;
	SegmentationInput in;
	{
		std::shared_lock lock(m_configMutex);
		engine = m_segEngine;
		in.config = m_aiConfig;
	}
	if (!engine) throw std::runtime_error("segmentation engine was removed");
	in.subject = subject;
	in.outputDirectory = m_options.outputDirectory;

	m_cohort.setState(subject.subjectId, ProcessingState::Segmenting);
	const ProgressFn scaled = [&](int p, const std::string& stage) {
		progress(progressFrom + (progressTo - progressFrom) * (std::max)(0, (std::min)(100, p)) / 100, stage);
	};
	SegmentationOutput out = engine->run(in, cancel, scaled);
	cancel.throwIfCancelled();

	{
		std::unique_lock lock(m_resultsMutex);
		m_segmentations[subject.subjectId] = out;
	}
	m_cohort.modify(subject.subjectId, [&](SubjectRecord& r) {
		if (!out.maskPath.empty()) r.segmentationPath = out.maskPath;
		r.state = ProcessingState::Segmented;
		r.lastError.clear();
	});
	return out;
}

MESIResultRecord MESIManager::doFusion(const SubjectRecord& subject, const CancellationToken& cancel,
	const ProgressFn& progress, int progressFrom, int progressTo)
{
	std::shared_ptr<IMESIFusionEngine> engine;
	FusionInput in;
	{
		std::shared_lock lock(m_configMutex);
		engine = m_fusionEngine;
		in.aiConfig = m_aiConfig;
	}
	if (!engine) throw std::runtime_error("fusion engine was removed");
	in.subject = m_cohort.find(subject.subjectId).value_or(subject);   // pick up segmentationPath
	in.outputDirectory = m_options.outputDirectory;
	{
		std::shared_lock lock(m_resultsMutex);
		auto it = m_segmentations.find(subject.subjectId);
		if (it != m_segmentations.end()) {
			in.segmentation = it->second.instances;
			in.segmentationMaskPath = it->second.maskPath;
		}
	}

	m_cohort.setState(subject.subjectId, ProcessingState::Fusing);
	const ProgressFn scaled = [&](int p, const std::string& stage) {
		progress(progressFrom + (progressTo - progressFrom) * (std::max)(0, (std::min)(100, p)) / 100, stage);
	};
	const auto t0 = std::chrono::steady_clock::now();
	MESIResultRecord result = engine->compute(in, cancel, scaled);
	cancel.throwIfCancelled();

	result.resultId = makeResultId(subject.subjectId);
	result.subjectId = subject.subjectId;
	if (result.modality == Modality::None) result.modality = in.subject.modality.modalities;
	result.aiModel = in.aiConfig.modelName + " v" + in.aiConfig.modelVersion;
	result.createdUtc = nowIso8601();
	if (result.computeSeconds <= 0.0)
		result.computeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	if (result.method.empty()) result.method = engine->name();

	storeResult(result);
	m_cohort.setState(subject.subjectId, ProcessingState::Completed);
	return result;
}

JobId MESIManager::runAISegmentationAsync(const std::string& subjectId, std::string* errorOut)
{
	SubjectRecord subject;
	if (!prepareJob(subjectId, true, false, subject, errorOut)) return kInvalidJobId;
	m_cohort.setState(subjectId, ProcessingState::Queued);
	return m_jobs.submit("segmentation", subjectId, [this, subject](const CancellationToken& cancel, const ProgressFn& progress) {
		doSegmentation(subject, cancel, progress, 0, 100);
	});
}

JobId MESIManager::computeMESIFusionAsync(const std::string& subjectId, std::string* errorOut)
{
	SubjectRecord subject;
	if (!prepareJob(subjectId, false, true, subject, errorOut)) return kInvalidJobId;
	m_cohort.setState(subjectId, ProcessingState::Queued);
	return m_jobs.submit("fusion", subjectId, [this, subject](const CancellationToken& cancel, const ProgressFn& progress) {
		doFusion(subject, cancel, progress, 0, 100);
	});
}

JobId MESIManager::runFullPipelineAsync(const std::string& subjectId, std::string* errorOut)
{
	SubjectRecord subject;
	if (!prepareJob(subjectId, true, true, subject, errorOut)) return kInvalidJobId;
	m_cohort.setState(subjectId, ProcessingState::Queued);
	return m_jobs.submit("pipeline", subjectId, [this, subject](const CancellationToken& cancel, const ProgressFn& progress) {
		doSegmentation(subject, cancel, progress, 0, 50);
		doFusion(subject, cancel, progress, 50, 100);
	});
}

std::vector<JobId> MESIManager::runFullPipelineForAll(const std::vector<std::string>& subjectIds, std::vector<std::string>* errorsOut)
{
	std::vector<JobId> queued;
	for (const auto& id : subjectIds) {
		std::string err;
		const JobId job = runFullPipelineAsync(id, &err);
		if (job != kInvalidJobId) queued.push_back(job);
		else if (errorsOut) errorsOut->push_back(id + ": " + err);
	}
	return queued;
}

bool MESIManager::cancelJob(JobId id) { return m_jobs.cancel(id); }
void MESIManager::cancelAllJobs() { m_jobs.cancelAll(); }
std::vector<JobInfo> MESIManager::activeJobs() const { return m_jobs.activeJobs(); }
std::optional<JobInfo> MESIManager::jobInfo(JobId id) const { return m_jobs.info(id); }
bool MESIManager::isSubjectBusy(const std::string& subjectId) const { return m_jobs.isSubjectBusy(subjectId); }
bool MESIManager::waitForIdle(std::chrono::milliseconds timeout) { return m_jobs.waitIdleFor(timeout); }

std::optional<SegmentationOutput> MESIManager::lastSegmentation(const std::string& subjectId) const
{
	std::shared_lock lock(m_resultsMutex);
	auto it = m_segmentations.find(subjectId);
	if (it == m_segmentations.end()) return std::nullopt;
	return it->second;
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------
std::string MESIManager::makeResultId(const std::string& subjectId)
{
	const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
	char buf[64];
	std::snprintf(buf, sizeof buf, "_%lld_%llu", static_cast<long long>(now),
		static_cast<unsigned long long>(m_resultCounter.fetch_add(1) + 1));
	return subjectId + buf;
}

void MESIManager::storeResult(MESIResultRecord record)
{
	const std::string subjectId = record.subjectId;
	{
		std::unique_lock lock(m_resultsMutex);
		m_results.push_back(std::move(record));
	}
	MesiEvent ev;
	ev.type = MesiEvent::Type::ResultsChanged;
	ev.subjectId = subjectId;
	emit(ev);
}

std::vector<MESIResultRecord> MESIManager::results() const
{
	std::shared_lock lock(m_resultsMutex);
	return m_results;
}

std::vector<MESIResultRecord> MESIManager::resultsForSubject(const std::string& subjectId) const
{
	std::shared_lock lock(m_resultsMutex);
	std::vector<MESIResultRecord> out;
	for (const auto& r : m_results)
		if (r.subjectId == subjectId) out.push_back(r);
	return out;
}

std::optional<MESIResultRecord> MESIManager::result(const std::string& resultId) const
{
	std::shared_lock lock(m_resultsMutex);
	for (const auto& r : m_results)
		if (r.resultId == resultId) return r;
	return std::nullopt;
}

bool MESIManager::addResult(MESIResultRecord record)
{
	if (record.subjectId.empty()) return false;
	if (record.resultId.empty()) record.resultId = makeResultId(record.subjectId);
	{
		std::shared_lock lock(m_resultsMutex);
		for (const auto& r : m_results)
			if (r.resultId == record.resultId) return false;
	}
	storeResult(std::move(record));
	return true;
}

bool MESIManager::setResultVerification(const std::string& resultId, VerificationStatus status,
	const std::string& reviewer, const std::string& notes)
{
	std::string subjectId;
	{
		std::unique_lock lock(m_resultsMutex);
		auto it = std::find_if(m_results.begin(), m_results.end(), [&](const MESIResultRecord& r) { return r.resultId == resultId; });
		if (it == m_results.end()) return false;
		it->setVerification(status, reviewer, notes);
		subjectId = it->subjectId;
	}
	MesiEvent ev;
	ev.type = MesiEvent::Type::ResultsChanged;
	ev.subjectId = subjectId;
	ev.message = resultId;
	emit(ev);
	return true;
}

bool MESIManager::removeResult(const std::string& resultId)
{
	std::string subjectId;
	{
		std::unique_lock lock(m_resultsMutex);
		auto it = std::find_if(m_results.begin(), m_results.end(), [&](const MESIResultRecord& r) { return r.resultId == resultId; });
		if (it == m_results.end()) return false;
		subjectId = it->subjectId;
		m_results.erase(it);
	}
	MesiEvent ev;
	ev.type = MesiEvent::Type::ResultsChanged;
	ev.subjectId = subjectId;
	emit(ev);
	return true;
}

std::size_t MESIManager::removeResultsForSubject(const std::string& subjectId)
{
	std::size_t removed = 0;
	{
		std::unique_lock lock(m_resultsMutex);
		const auto before = m_results.size();
		m_results.erase(std::remove_if(m_results.begin(), m_results.end(),
			[&](const MESIResultRecord& r) { return r.subjectId == subjectId; }), m_results.end());
		m_segmentations.erase(subjectId);
		removed = before - m_results.size();
	}
	if (removed) {
		MesiEvent ev;
		ev.type = MesiEvent::Type::ResultsChanged;
		ev.subjectId = subjectId;
		emit(ev);
	}
	return removed;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
const char* MESIManager::exportExtension(ExportFormat format) noexcept
{
	switch (format) {
	case ExportFormat::Json: return "json";
	case ExportFormat::Csv:  return "csv";
	case ExportFormat::Xml:  return "xml";
	}
	return "dat";
}

bool MESIManager::exportResults(const std::string& utf8Path, ExportFormat format,
	const std::string& subjectFilter, std::string* errorOut) const
{
	const std::vector<MESIResultRecord> rows = subjectFilter.empty() ? results() : resultsForSubject(subjectFilter);
	std::string text;
	switch (format) {
	case ExportFormat::Json: {
		JsonValue arr = JsonValue::array();
		for (const auto& r : rows) arr.push(r.toJson());
		text = JsonValue::object()
			.set("format", "MESI-Results")
			.set("schemaVersion", SubjectCohortManager::kSchemaVersion)
			.set("exportedUtc", nowIso8601())
			.set("results", std::move(arr)).dump(2);
		break;
	}
	case ExportFormat::Csv: {
		text = MESIResultRecord::csvHeader();
		for (const auto& r : rows) r.appendCsvRows(text);
		break;
	}
	case ExportFormat::Xml: {
		XmlNode root("MESIResults");
		root.attr("schemaVersion", static_cast<long long>(SubjectCohortManager::kSchemaVersion)).attr("exportedUtc", nowIso8601());
		for (const auto& r : rows) root.child(r.toXml());
		text = root.dump();
		break;
	}
	}
	if (!writeTextFile(utf8Path, text)) {
		if (errorOut) *errorOut = "Cannot write " + utf8Path;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------
bool MESIManager::saveWorkspace(const std::string& dir, std::string* errorOut) const
{
	if (!m_cohort.saveJsonFile(joinPath(dir, kSubjectsFile), errorOut)) return false;
	if (!exportResults(joinPath(dir, kResultsFile), ExportFormat::Json, {}, errorOut)) return false;
	if (!writeTextFile(joinPath(dir, kConfigFile), aiConfig().toJson().dump(2))) {
		if (errorOut) *errorOut = "Cannot write " + joinPath(dir, kConfigFile);
		return false;
	}
	return true;
}

bool MESIManager::loadWorkspace(const std::string& dir, std::string* errorOut)
{
	if (m_jobs.activeCount() != 0) {
		if (errorOut) *errorOut = "Cannot load a workspace while jobs are running.";
		return false;
	}
	if (!m_cohort.loadJsonFile(joinPath(dir, kSubjectsFile), errorOut)) return false;

	// Results are optional in a workspace.
	if (const auto text = readTextFile(joinPath(dir, kResultsFile))) {
		try {
			const JsonValue doc = JsonValue::parse(*text);
			std::vector<MESIResultRecord> loaded;
			if (doc["results"].isArray())
				for (const auto& j : doc["results"].asArray()) loaded.push_back(MESIResultRecord::fromJson(j));
			{
				std::unique_lock lock(m_resultsMutex);
				m_results = std::move(loaded);
				m_segmentations.clear();
			}
		} catch (const std::exception& e) {
			if (errorOut) *errorOut = std::string("results.json: ") + e.what();
			return false;
		}
	} else {
		std::unique_lock lock(m_resultsMutex);
		m_results.clear();
		m_segmentations.clear();
	}
	if (const auto text = readTextFile(joinPath(dir, kConfigFile))) {
		try {
			const AISegmentationConfig cfg = AISegmentationConfig::fromJson(JsonValue::parse(*text));
			std::unique_lock lock(m_configMutex);
			m_aiConfig = cfg;      // accept as-is; the UI re-validates before running
		} catch (const std::exception& e) {
			if (errorOut) *errorOut = std::string("ai_config.json: ") + e.what();
			return false;
		}
	}
	MesiEvent ev;
	ev.type = MesiEvent::Type::ResultsChanged;
	emit(ev);
	ev.type = MesiEvent::Type::ConfigChanged;
	emit(ev);
	return true;
}

} // namespace mesi
