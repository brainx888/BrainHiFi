#include "SubjectCohortManager.h"

#include <algorithm>
#include <mutex>

namespace mesi {

namespace {
std::string joinErrors(const std::vector<std::string>& errors)
{
	std::string out;
	for (const auto& e : errors) { if (!out.empty()) out += ' '; out += e; }
	return out;
}
} // namespace

SubjectCohortManager::Records::iterator SubjectCohortManager::findIt(std::string_view id)
{
	return std::find_if(m_records.begin(), m_records.end(), [&](const SubjectRecord& r) { return r.subjectId == id; });
}
SubjectCohortManager::Records::const_iterator SubjectCohortManager::findIt(std::string_view id) const
{
	return std::find_if(m_records.begin(), m_records.end(), [&](const SubjectRecord& r) { return r.subjectId == id; });
}

void SubjectCohortManager::notify(ChangeKind kind, const std::string& id) const
{
	ChangeListener listener;
	{
		std::shared_lock lock(m_listenerMutex);
		listener = m_listener;
	}
	if (listener) listener(kind, id);
}

void SubjectCohortManager::setChangeListener(ChangeListener listener)
{
	std::unique_lock lock(m_listenerMutex);
	m_listener = std::move(listener);
}

// Mutation ------------------------------------------------------------------

bool SubjectCohortManager::add(SubjectRecord record, std::string* errorOut)
{
	if (record.cohort == CohortGroup::Unknown) record.deriveCohortFromAge();
	if (const auto errors = record.validate(); !errors.empty()) {
		if (errorOut) *errorOut = joinErrors(errors);
		return false;
	}
	{
		std::unique_lock lock(m_mutex);
		if (findIt(record.subjectId) != m_records.end()) {
			if (errorOut) *errorOut = "Subject ID '" + record.subjectId + "' already exists.";
			return false;
		}
		m_records.push_back(std::move(record));
	}
	notify(ChangeKind::Added, m_records.back().subjectId);
	return true;
}

bool SubjectCohortManager::update(const SubjectRecord& record, std::string* errorOut)
{
	if (const auto errors = record.validate(); !errors.empty()) {
		if (errorOut) *errorOut = joinErrors(errors);
		return false;
	}
	{
		std::unique_lock lock(m_mutex);
		auto it = findIt(record.subjectId);
		if (it == m_records.end()) {
			if (errorOut) *errorOut = "Subject ID '" + record.subjectId + "' not found.";
			return false;
		}
		*it = record;
		it->touch();
	}
	notify(ChangeKind::Updated, record.subjectId);
	return true;
}

bool SubjectCohortManager::modify(std::string_view subjectId, const std::function<void(SubjectRecord&)>& fn)
{
	std::string id;
	{
		std::unique_lock lock(m_mutex);
		auto it = findIt(subjectId);
		if (it == m_records.end()) return false;
		fn(*it);
		it->subjectId = std::string(subjectId);   // ID is the key; never let a callback change it
		it->touch();
		id = it->subjectId;
	}
	notify(ChangeKind::Updated, id);
	return true;
}

bool SubjectCohortManager::remove(std::string_view subjectId)
{
	{
		std::unique_lock lock(m_mutex);
		auto it = findIt(subjectId);
		if (it == m_records.end()) return false;
		m_records.erase(it);
	}
	notify(ChangeKind::Removed, std::string(subjectId));
	return true;
}

void SubjectCohortManager::clear()
{
	{
		std::unique_lock lock(m_mutex);
		m_records.clear();
	}
	notify(ChangeKind::Reloaded, {});
}

bool SubjectCohortManager::setState(std::string_view subjectId, ProcessingState state, std::string error)
{
	return modify(subjectId, [&](SubjectRecord& r) {
		r.state = state;
		r.lastError = std::move(error);
	});
}

// Queries -------------------------------------------------------------------

std::size_t SubjectCohortManager::size() const
{
	std::shared_lock lock(m_mutex);
	return m_records.size();
}

bool SubjectCohortManager::contains(std::string_view subjectId) const
{
	std::shared_lock lock(m_mutex);
	return findIt(subjectId) != m_records.end();
}

std::optional<SubjectRecord> SubjectCohortManager::find(std::string_view subjectId) const
{
	std::shared_lock lock(m_mutex);
	auto it = findIt(subjectId);
	if (it == m_records.end()) return std::nullopt;
	return *it;
}

std::vector<SubjectRecord> SubjectCohortManager::all() const
{
	std::shared_lock lock(m_mutex);
	return m_records;
}

std::vector<std::string> SubjectCohortManager::ids() const
{
	std::shared_lock lock(m_mutex);
	std::vector<std::string> out;
	out.reserve(m_records.size());
	for (const auto& r : m_records) out.push_back(r.subjectId);
	return out;
}

std::vector<SubjectRecord> SubjectCohortManager::filter(const Predicate& pred) const
{
	std::shared_lock lock(m_mutex);
	std::vector<SubjectRecord> out;
	for (const auto& r : m_records)
		if (pred(r)) out.push_back(r);
	return out;
}

std::vector<SubjectRecord> SubjectCohortManager::queryByAgeRange(double minYears, double maxYears) const
{
	if (minYears > maxYears) std::swap(minYears, maxYears);
	return filter([=](const SubjectRecord& r) { return r.ageYears >= minYears && r.ageYears <= maxYears; });
}

std::vector<SubjectRecord> SubjectCohortManager::queryByCohort(CohortGroup group) const
{
	return filter([=](const SubjectRecord& r) { return r.cohort == group; });
}

std::vector<SubjectRecord> SubjectCohortManager::queryByModality(Modality mask, bool matchAll) const
{
	if (mask == Modality::None) return {};
	return filter([=](const SubjectRecord& r) {
		return matchAll ? hasModality(r.modality.modalities, mask) : hasAnyModality(r.modality.modalities, mask);
	});
}

std::vector<SubjectRecord> SubjectCohortManager::queryByState(ProcessingState state) const
{
	return filter([=](const SubjectRecord& r) { return r.state == state; });
}

SubjectCohortManager::Statistics SubjectCohortManager::statistics() const
{
	std::shared_lock lock(m_mutex);
	Statistics s;
	s.total = m_records.size();
	double sum = 0.0;
	for (std::size_t i = 0; i < m_records.size(); ++i) {
		const SubjectRecord& r = m_records[i];
		switch (r.cohort) {
		case CohortGroup::Pediatric: ++s.pediatric; break;
		case CohortGroup::Adult:     ++s.adult; break;
		default:                     ++s.unknownCohort; break;
		}
		if (r.modality.isCombined()) ++s.combined;
		else if (r.modality.hasMEG()) ++s.meg;
		else if (r.modality.hasEEG()) ++s.eeg;
		if (r.state == ProcessingState::Completed) ++s.completed;
		else if (r.state == ProcessingState::Failed) ++s.failed;
		else if (isBusyState(r.state)) ++s.busy;
		sum += r.ageYears;
		if (i == 0) { s.minAge = s.maxAge = r.ageYears; }
		else { s.minAge = (std::min)(s.minAge, r.ageYears); s.maxAge = (std::max)(s.maxAge, r.ageYears); }
	}
	s.meanAge = s.total ? sum / static_cast<double>(s.total) : 0.0;
	return s;
}

// Serialization -------------------------------------------------------------

bool SubjectCohortManager::replaceAll(Records records)
{
	{
		std::unique_lock lock(m_mutex);
		m_records = std::move(records);
	}
	notify(ChangeKind::Reloaded, {});
	return true;
}

JsonValue SubjectCohortManager::toJson() const
{
	JsonValue subjects = JsonValue::array();
	{
		std::shared_lock lock(m_mutex);
		for (const auto& r : m_records) subjects.push(r.toJson());
	}
	return JsonValue::object()
		.set("format", "MESI-Cohort")
		.set("schemaVersion", kSchemaVersion)
		.set("savedUtc", nowIso8601())
		.set("subjects", std::move(subjects));
}

bool SubjectCohortManager::fromJson(const JsonValue& doc, std::vector<std::string>* skippedOut)
{
	if (!doc.isObject() || !doc["subjects"].isArray()) return false;
	if (doc["schemaVersion"].asInt(kSchemaVersion) > kSchemaVersion) return false;   // newer writer
	Records records;
	for (const auto& j : doc["subjects"].asArray()) {
		SubjectRecord r = SubjectRecord::fromJson(j);
		const auto errors = r.validate();
		const bool duplicate = std::any_of(records.begin(), records.end(),
			[&](const SubjectRecord& x) { return x.subjectId == r.subjectId; });
		if (!errors.empty() || duplicate) {
			if (skippedOut) skippedOut->push_back(r.subjectId + ": " + (duplicate ? "duplicate ID" : joinErrors(errors)));
			continue;
		}
		records.push_back(std::move(r));
	}
	return replaceAll(std::move(records));
}

bool SubjectCohortManager::fromJsonString(std::string_view text, std::string* errorOut)
{
	if (errorOut) errorOut->clear();
	try {
		std::vector<std::string> skipped;
		if (!fromJson(JsonValue::parse(text), &skipped)) {
			if (errorOut) *errorOut = "Not a MESI cohort document (or newer schema).";
			return false;
		}
		if (errorOut && !skipped.empty()) *errorOut = "Skipped " + std::to_string(skipped.size()) + " invalid record(s): " + joinErrors(skipped);
		return true;
	} catch (const std::exception& e) {
		if (errorOut) *errorOut = e.what();
		return false;
	}
}

XmlNode SubjectCohortManager::toXml() const
{
	XmlNode root("MESICohort");
	root.attr("schemaVersion", static_cast<long long>(kSchemaVersion)).attr("savedUtc", nowIso8601());
	std::shared_lock lock(m_mutex);
	for (const auto& r : m_records) root.child(r.toXml());
	return root;
}

bool SubjectCohortManager::fromXml(const XmlNode& root, std::vector<std::string>* skippedOut)
{
	if (root.name != "MESICohort") return false;
	if (root.attributeInt("schemaVersion", kSchemaVersion) > kSchemaVersion) return false;
	Records records;
	for (const XmlNode* n : root.childrenNamed("Subject")) {
		SubjectRecord r = SubjectRecord::fromXml(*n);
		const auto errors = r.validate();
		const bool duplicate = std::any_of(records.begin(), records.end(),
			[&](const SubjectRecord& x) { return x.subjectId == r.subjectId; });
		if (!errors.empty() || duplicate) {
			if (skippedOut) skippedOut->push_back(r.subjectId + ": " + (duplicate ? "duplicate ID" : joinErrors(errors)));
			continue;
		}
		records.push_back(std::move(r));
	}
	return replaceAll(std::move(records));
}

bool SubjectCohortManager::fromXmlString(std::string_view text, std::string* errorOut)
{
	if (errorOut) errorOut->clear();
	try {
		std::vector<std::string> skipped;
		if (!fromXml(XmlNode::parse(text), &skipped)) {
			if (errorOut) *errorOut = "Not a MESI cohort XML document (or newer schema).";
			return false;
		}
		if (errorOut && !skipped.empty()) *errorOut = "Skipped " + std::to_string(skipped.size()) + " invalid record(s): " + joinErrors(skipped);
		return true;
	} catch (const std::exception& e) {
		if (errorOut) *errorOut = e.what();
		return false;
	}
}

bool SubjectCohortManager::saveJsonFile(const std::string& utf8Path, std::string* errorOut) const
{
	if (writeTextFile(utf8Path, toJsonString())) return true;
	if (errorOut) *errorOut = "Cannot write " + utf8Path;
	return false;
}

bool SubjectCohortManager::loadJsonFile(const std::string& utf8Path, std::string* errorOut)
{
	const auto text = readTextFile(utf8Path);
	if (!text) { if (errorOut) *errorOut = "Cannot read " + utf8Path; return false; }
	return fromJsonString(*text, errorOut);
}

bool SubjectCohortManager::saveXmlFile(const std::string& utf8Path, std::string* errorOut) const
{
	if (writeTextFile(utf8Path, toXmlString())) return true;
	if (errorOut) *errorOut = "Cannot write " + utf8Path;
	return false;
}

bool SubjectCohortManager::loadXmlFile(const std::string& utf8Path, std::string* errorOut)
{
	const auto text = readTextFile(utf8Path);
	if (!text) { if (errorOut) *errorOut = "Cannot read " + utf8Path; return false; }
	return fromXmlString(*text, errorOut);
}

} // namespace mesi
