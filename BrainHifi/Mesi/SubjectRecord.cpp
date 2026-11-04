#include "SubjectRecord.h"

namespace mesi {

std::vector<std::string> SubjectRecord::validate() const
{
	std::vector<std::string> errors;
	if (subjectId.empty()) errors.push_back("Subject ID is empty.");
	if (subjectId.size() > kMaxIdLength) errors.push_back("Subject ID is longer than " + std::to_string(kMaxIdLength) + " characters.");
	for (const char c : subjectId)
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			errors.push_back("Subject ID contains a character that is not allowed in file names.");
			break;
		}
	if (ageYears < 0.0 || ageYears > kMaxAgeYears) errors.push_back("Age must be between 0 and 120 years.");
	if (cohort != CohortGroup::Unknown && cohort != cohortFromAge(ageYears))
		errors.push_back("Cohort group does not match the subject's age.");
	if (t1MriPath.empty() && modality.coregisteredMriPath.empty())
		errors.push_back("No anatomical MRI path (T1 or co-registered volume).");
	return errors;
}

JsonValue SubjectRecord::toJson() const
{
	return JsonValue::object()
		.set("subjectId", subjectId)
		.set("ageYears", ageYears)
		.set("cohort", toString(cohort))
		.set("sex", sex)
		.set("clinicalNotes", clinicalNotes)
		.set("diagnosis", diagnosis)
		.set("anatomy", JsonValue::object()
			.set("t1MriPath", t1MriPath)
			.set("t2MriPath", t2MriPath)
			.set("segmentationPath", segmentationPath)
			.set("atlasPath", atlasPath))
		.set("modality", modality.toJson())
		.set("state", toString(state))
		.set("lastError", lastError)
		.set("createdUtc", createdUtc)
		.set("modifiedUtc", modifiedUtc);
}

SubjectRecord SubjectRecord::fromJson(const JsonValue& j)
{
	SubjectRecord r;
	r.subjectId = j["subjectId"].asString();
	r.ageYears = j["ageYears"].asDouble();
	r.cohort = cohortFromString(j["cohort"].asString()).value_or(cohortFromAge(r.ageYears));
	r.sex = j["sex"].asString();
	r.clinicalNotes = j["clinicalNotes"].asString();
	r.diagnosis = j["diagnosis"].asString();
	const JsonValue& a = j["anatomy"];
	r.t1MriPath = a["t1MriPath"].asString();
	r.t2MriPath = a["t2MriPath"].asString();
	r.segmentationPath = a["segmentationPath"].asString();
	r.atlasPath = a["atlasPath"].asString();
	r.modality = ModalityConfig::fromJson(j["modality"]);
	// A busy state can never be valid after a reload: the job no longer exists.
	r.state = processingStateFromString(j["state"].asString()).value_or(ProcessingState::Idle);
	if (isBusyState(r.state)) r.state = ProcessingState::Idle;
	r.lastError = j["lastError"].asString();
	r.createdUtc = j["createdUtc"].asString(r.createdUtc);
	r.modifiedUtc = j["modifiedUtc"].asString(r.modifiedUtc);
	return r;
}

XmlNode SubjectRecord::toXml() const
{
	XmlNode n("Subject");
	n.attr("id", subjectId)
	 .attr("ageYears", ageYears)
	 .attr("cohort", toString(cohort))
	 .attr("sex", sex)
	 .attr("state", toString(state))
	 .attr("createdUtc", createdUtc)
	 .attr("modifiedUtc", modifiedUtc);
	n.childText("Diagnosis", diagnosis)
	 .childText("ClinicalNotes", clinicalNotes)
	 .childText("LastError", lastError);
	XmlNode anat("Anatomy");
	anat.childText("T1MriPath", t1MriPath)
		.childText("T2MriPath", t2MriPath)
		.childText("SegmentationPath", segmentationPath)
		.childText("AtlasPath", atlasPath);
	n.child(std::move(anat));
	n.child(modality.toXml());
	return n;
}

SubjectRecord SubjectRecord::fromXml(const XmlNode& n)
{
	SubjectRecord r;
	r.subjectId = n.attribute("id").value_or("");
	r.ageYears = n.attributeDouble("ageYears");
	r.cohort = cohortFromString(n.attribute("cohort").value_or("")).value_or(cohortFromAge(r.ageYears));
	r.sex = n.attribute("sex").value_or("");
	r.state = processingStateFromString(n.attribute("state").value_or("")).value_or(ProcessingState::Idle);
	if (isBusyState(r.state)) r.state = ProcessingState::Idle;
	r.createdUtc = n.attribute("createdUtc").value_or(r.createdUtc);
	r.modifiedUtc = n.attribute("modifiedUtc").value_or(r.modifiedUtc);
	r.diagnosis = n.childText("Diagnosis");
	r.clinicalNotes = n.childText("ClinicalNotes");
	r.lastError = n.childText("LastError");
	if (const XmlNode* anat = n.firstChild("Anatomy")) {
		r.t1MriPath = anat->childText("T1MriPath");
		r.t2MriPath = anat->childText("T2MriPath");
		r.segmentationPath = anat->childText("SegmentationPath");
		r.atlasPath = anat->childText("AtlasPath");
	}
	if (const XmlNode* mod = n.firstChild("ModalityConfig")) r.modality = ModalityConfig::fromXml(*mod);
	return r;
}

} // namespace mesi
