// SubjectCohortManager.h - thread-safe collection of SubjectRecord objects
// with filtering, age / modality queries and JSON / XML persistence.
//
// All accessors return copies so callers never hold references into the
// internal container while another thread (e.g. an inference worker updating
// a processing state) mutates it.
#pragma once

#include "SubjectRecord.h"

#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mesi {

class SubjectCohortManager
{
public:
	enum class ChangeKind { Added, Updated, Removed, Reloaded };
	using ChangeListener = std::function<void(ChangeKind, const std::string& subjectId)>;
	using Predicate = std::function<bool(const SubjectRecord&)>;

	SubjectCohortManager() = default;
	SubjectCohortManager(const SubjectCohortManager&) = delete;
	SubjectCohortManager& operator=(const SubjectCohortManager&) = delete;

	// Mutation --------------------------------------------------------------
	// Returns false (and leaves the cohort untouched) when the record fails
	// SubjectRecord::validate() or the ID already exists.
	bool add(SubjectRecord record, std::string* errorOut = nullptr);
	// Replace the record with the same subjectId; false when unknown / invalid.
	bool update(const SubjectRecord& record, std::string* errorOut = nullptr);
	// Apply a mutation to one record under the write lock; false when unknown.
	bool modify(std::string_view subjectId, const std::function<void(SubjectRecord&)>& fn);
	bool remove(std::string_view subjectId);
	void clear();
	bool setState(std::string_view subjectId, ProcessingState state, std::string error = {});

	// Queries (all return copies) --------------------------------------------
	std::size_t size() const;
	bool contains(std::string_view subjectId) const;
	std::optional<SubjectRecord> find(std::string_view subjectId) const;
	std::vector<SubjectRecord> all() const;
	std::vector<std::string> ids() const;
	std::vector<SubjectRecord> filter(const Predicate& pred) const;
	std::vector<SubjectRecord> queryByAgeRange(double minYearsInclusive, double maxYearsInclusive) const;
	std::vector<SubjectRecord> queryByCohort(CohortGroup group) const;
	// matchAll=true: subject must have every flag in `mask`; false: any flag.
	std::vector<SubjectRecord> queryByModality(Modality mask, bool matchAll = true) const;
	std::vector<SubjectRecord> queryByState(ProcessingState state) const;

	struct Statistics
	{
		std::size_t total = 0, pediatric = 0, adult = 0, unknownCohort = 0;
		std::size_t meg = 0, eeg = 0, combined = 0;
		std::size_t completed = 0, failed = 0, busy = 0;
		double minAge = 0.0, maxAge = 0.0, meanAge = 0.0;
	};
	Statistics statistics() const;

	// Serialization ---------------------------------------------------------
	static constexpr int kSchemaVersion = 1;

	JsonValue toJson() const;
	// Replaces the whole cohort.  Invalid records are skipped and reported in
	// `skippedOut`; returns false only when the document itself is unusable.
	bool fromJson(const JsonValue& doc, std::vector<std::string>* skippedOut = nullptr);
	std::string toJsonString() const { return toJson().dump(2); }
	bool fromJsonString(std::string_view text, std::string* errorOut = nullptr);

	XmlNode toXml() const;
	bool fromXml(const XmlNode& root, std::vector<std::string>* skippedOut = nullptr);
	std::string toXmlString() const { return toXml().dump(); }
	bool fromXmlString(std::string_view text, std::string* errorOut = nullptr);

	bool saveJsonFile(const std::string& utf8Path, std::string* errorOut = nullptr) const;
	bool loadJsonFile(const std::string& utf8Path, std::string* errorOut = nullptr);
	bool saveXmlFile(const std::string& utf8Path, std::string* errorOut = nullptr) const;
	bool loadXmlFile(const std::string& utf8Path, std::string* errorOut = nullptr);

	// Change notification (invoked on the mutating thread, outside the lock).
	void setChangeListener(ChangeListener listener);

private:
	using Records = std::vector<SubjectRecord>;

	Records::iterator findIt(std::string_view id);
	Records::const_iterator findIt(std::string_view id) const;
	void notify(ChangeKind kind, const std::string& id) const;
	bool replaceAll(Records records);

	mutable std::shared_mutex m_mutex;
	Records m_records;                 // small N; linear search keeps order stable
	ChangeListener m_listener;
	mutable std::shared_mutex m_listenerMutex;
};

} // namespace mesi
