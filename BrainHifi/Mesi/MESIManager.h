// MESIManager.h - central facade of the Multimodal Source Imaging management
// system.  Owns the subject cohort, the AI segmentation configuration, the
// result store and the background job runner, and coordinates the
// segmentation -> fusion pipeline for single subjects or whole cohorts.
//
// Thread model
//   * All public methods are safe to call from any thread.
//   * Engines execute on MesiJobRunner worker threads (Options::workerThreads,
//     default 1 so CUDA / ONNX sessions are never used concurrently).
//   * Events (MesiEvent) are delivered on the thread that caused them - for
//     job events that is a worker thread.  GUI code must marshal them
//     (see CMesiWorkspaceDlg::postMesiEvent).
#pragma once

#include "AISegmentationConfig.h"
#include "MESIResultRecord.h"
#include "MesiEngines.h"
#include "MesiJobRunner.h"
#include "SubjectCohortManager.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace mesi {

struct MesiEvent
{
	enum class Type : std::uint8_t
	{
		SubjectsChanged,   // cohort added / updated / removed / reloaded
		JobQueued,
		JobStarted,
		JobProgress,
		JobCompleted,
		JobFailed,
		JobCancelled,
		ResultsChanged,    // result added / verified / removed / reloaded
		ConfigChanged,
	};
	Type type = Type::SubjectsChanged;
	JobId jobId = kInvalidJobId;
	std::string kind;        // "segmentation" / "fusion" for job events
	std::string subjectId;
	int percent = 0;
	std::string message;

	bool isJobEvent() const noexcept { return jobId != kInvalidJobId; }
};

class MESIManager
{
public:
	using EventSink = std::function<void(const MesiEvent&)>;
	enum class ExportFormat : std::uint8_t { Json, Csv, Xml };

	struct Options
	{
		std::size_t workerThreads = 1;      // GPU pipelines: keep 1 per device
		std::string outputDirectory;        // where engines may write volumes (UTF-8)
	};

	MESIManager() : MESIManager(Options()) {}
	explicit MESIManager(Options options);
	~MESIManager();                          // cancels running jobs and joins
	MESIManager(const MESIManager&) = delete;
	MESIManager& operator=(const MESIManager&) = delete;

	// Sub-systems -----------------------------------------------------------
	SubjectCohortManager& cohort() noexcept { return m_cohort; }
	const SubjectCohortManager& cohort() const noexcept { return m_cohort; }
	const Options& options() const noexcept { return m_options; }

	AISegmentationConfig aiConfig() const;
	// Returns validation errors (empty on success); invalid configs are rejected.
	std::vector<std::string> setAiConfig(const AISegmentationConfig& cfg);

	void setSegmentationEngine(std::shared_ptr<IAISegmentationEngine> engine);
	void setFusionEngine(std::shared_ptr<IMESIFusionEngine> engine);
	std::shared_ptr<IAISegmentationEngine> segmentationEngine() const;
	std::shared_ptr<IMESIFusionEngine> fusionEngine() const;

	// Pipeline --------------------------------------------------------------
	// Queue a job; kInvalidJobId with `errorOut` set when the subject is
	// unknown / busy / misconfigured or no engine is installed.
	JobId runAISegmentationAsync(const std::string& subjectId, std::string* errorOut = nullptr);
	JobId computeMESIFusionAsync(const std::string& subjectId, std::string* errorOut = nullptr);
	// Segmentation immediately followed by fusion in one job.
	JobId runFullPipelineAsync(const std::string& subjectId, std::string* errorOut = nullptr);
	// Convenience for cohorts; returns the ids that were queued.
	std::vector<JobId> runFullPipelineForAll(const std::vector<std::string>& subjectIds, std::vector<std::string>* errorsOut = nullptr);

	bool cancelJob(JobId id);
	void cancelAllJobs();
	std::vector<JobInfo> activeJobs() const;
	std::optional<JobInfo> jobInfo(JobId id) const;
	bool isSubjectBusy(const std::string& subjectId) const;
	bool waitForIdle(std::chrono::milliseconds timeout);

	std::optional<SegmentationOutput> lastSegmentation(const std::string& subjectId) const;

	// Results ---------------------------------------------------------------
	std::vector<MESIResultRecord> results() const;
	std::vector<MESIResultRecord> resultsForSubject(const std::string& subjectId) const;
	std::optional<MESIResultRecord> result(const std::string& resultId) const;
	bool addResult(MESIResultRecord record);     // external / imported results
	bool setResultVerification(const std::string& resultId, VerificationStatus status,
		const std::string& reviewer, const std::string& notes = {});
	bool removeResult(const std::string& resultId);
	std::size_t removeResultsForSubject(const std::string& subjectId);

	// Export ----------------------------------------------------------------
	// `subjectFilter` empty = all subjects.
	bool exportResults(const std::string& utf8Path, ExportFormat format,
		const std::string& subjectFilter = {}, std::string* errorOut = nullptr) const;
	static const char* exportExtension(ExportFormat format) noexcept;

	// Workspace persistence -------------------------------------------------
	// Writes subjects.json, results.json and ai_config.json into `directory`.
	bool saveWorkspace(const std::string& utf8Directory, std::string* errorOut = nullptr) const;
	bool loadWorkspace(const std::string& utf8Directory, std::string* errorOut = nullptr);

	// Events ----------------------------------------------------------------
	void setEventSink(EventSink sink);

private:
	struct SegmentationStep;
	struct FusionStep;

	void emit(const MesiEvent& ev) const;
	void onJobUpdate(const JobInfo& info);
	void onCohortChange(SubjectCohortManager::ChangeKind kind, const std::string& id);

	bool prepareJob(const std::string& subjectId, bool needSegEngine, bool needFusionEngine,
		SubjectRecord& subjectOut, std::string* errorOut) const;
	SegmentationOutput doSegmentation(const SubjectRecord& subject, const CancellationToken& cancel,
		const ProgressFn& progress, int progressFrom, int progressTo);
	MESIResultRecord doFusion(const SubjectRecord& subject, const CancellationToken& cancel,
		const ProgressFn& progress, int progressFrom, int progressTo);
	std::string makeResultId(const std::string& subjectId);
	void storeResult(MESIResultRecord record);

	Options m_options;
	SubjectCohortManager m_cohort;
	MesiJobRunner m_jobs;

	mutable std::shared_mutex m_configMutex;
	AISegmentationConfig m_aiConfig;
	std::shared_ptr<IAISegmentationEngine> m_segEngine;
	std::shared_ptr<IMESIFusionEngine> m_fusionEngine;

	mutable std::shared_mutex m_resultsMutex;
	std::vector<MESIResultRecord> m_results;
	std::map<std::string, SegmentationOutput> m_segmentations;   // by subjectId
	std::atomic<std::uint64_t> m_resultCounter{ 0 };

	mutable std::shared_mutex m_sinkMutex;
	EventSink m_sink;
};

} // namespace mesi
