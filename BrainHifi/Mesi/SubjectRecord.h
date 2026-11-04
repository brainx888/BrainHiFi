// SubjectRecord.h - one subject of the MESI cohort.
#pragma once

#include "MesiTypes.h"
#include "ModalityConfig.h"
#include "MesiJson.h"
#include "MesiXml.h"

#include <string>
#include <vector>

namespace mesi {

struct SubjectRecord
{
	static constexpr double kMinAgeYears = 0.0;
	static constexpr double kMaxAgeYears = mesi::kMaxAgeYears;
	static constexpr std::size_t kMaxIdLength = 64;   // keeps file names / list columns sane

	std::string subjectId;          // unique, non-empty (pseudonymised study ID)
	double ageYears = 0.0;          // age at recording
	CohortGroup cohort = CohortGroup::Unknown;
	std::string sex;                // "F", "M", "" (free text, optional)
	std::string clinicalNotes;      // free text
	std::string diagnosis;          // short label, optional

	// Anatomical path references (UTF-8).  Co-registered volumes are part of
	// ModalityConfig; these are the raw / derived anatomy of the subject.
	std::string t1MriPath;
	std::string t2MriPath;
	std::string segmentationPath;   // AI tissue / cluster segmentation output
	std::string atlasPath;          // subject-space atlas labels, optional

	ModalityConfig modality;
	ProcessingState state = ProcessingState::Idle;
	std::string lastError;
	std::string createdUtc = nowIso8601();
	std::string modifiedUtc = createdUtc;

	bool isPediatric() const noexcept { return cohort == CohortGroup::Pediatric; }
	bool isAdult() const noexcept { return cohort == CohortGroup::Adult; }

	// Keeps `cohort` consistent with `ageYears`.
	void deriveCohortFromAge() noexcept { cohort = cohortFromAge(ageYears); }
	void touch() { modifiedUtc = nowIso8601(); }

	std::vector<std::string> validate() const;

	JsonValue toJson() const;
	static SubjectRecord fromJson(const JsonValue& j);
	XmlNode toXml() const;
	static SubjectRecord fromXml(const XmlNode& n);
};

} // namespace mesi
