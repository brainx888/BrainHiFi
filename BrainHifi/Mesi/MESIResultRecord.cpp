#include "MESIResultRecord.h"

#include <cstdio>

namespace mesi {

namespace {

JsonValue vecToJson(const Vec3& v) { return JsonValue::Array{ v.x, v.y, v.z }; }

Vec3 vecFromJson(const JsonValue& j)
{
	if (j.size() != 3) return {};
	return { j.at(0).asDouble(), j.at(1).asDouble(), j.at(2).asDouble() };
}

std::string csvQuote(const std::string& s)
{
	bool needsQuote = false;
	for (const char c : s)
		if (c == ',' || c == '"' || c == '\n' || c == '\r') { needsQuote = true; break; }
	if (!needsQuote) return s;
	std::string out = "\"";
	for (const char c : s) { if (c == '"') out += "\"\""; else out.push_back(c); }
	out += "\"";
	return out;
}

std::string fmt(double v)
{
	char buf[40];
	std::snprintf(buf, sizeof buf, "%.6g", v);
	return buf;
}

} // namespace

// SourceCluster --------------------------------------------------------------

JsonValue SourceCluster::toJson() const
{
	return JsonValue::object()
		.set("label", label)
		.set("centroidMm", vecToJson(centroidMm))
		.set("peakMm", vecToJson(peakMm))
		.set("peakValue", peakValue)
		.set("volumeMm3", volumeMm3)
		.set("voxelCount", voxelCount)
		.set("confidence", confidence)
		.set("anatomicalLabel", anatomicalLabel)
		.set("functionalLabel", functionalLabel);
}

SourceCluster SourceCluster::fromJson(const JsonValue& j)
{
	SourceCluster c;
	c.label = j["label"].asInt();
	c.centroidMm = vecFromJson(j["centroidMm"]);
	c.peakMm = vecFromJson(j["peakMm"]);
	c.peakValue = j["peakValue"].asDouble();
	c.volumeMm3 = j["volumeMm3"].asDouble();
	c.voxelCount = j["voxelCount"].asInt();
	c.confidence = j["confidence"].asDouble();
	c.anatomicalLabel = j["anatomicalLabel"].asString();
	c.functionalLabel = j["functionalLabel"].asString();
	return c;
}

// DipoleMoment ---------------------------------------------------------------

JsonValue DipoleMoment::toJson() const
{
	return JsonValue::object()
		.set("positionMm", vecToJson(positionMm))
		.set("momentNAm", vecToJson(momentNAm))
		.set("latencyMs", latencyMs)
		.set("goodnessOfFit", goodnessOfFit)
		.set("clusterLabel", clusterLabel);
}

DipoleMoment DipoleMoment::fromJson(const JsonValue& j)
{
	DipoleMoment d;
	d.positionMm = vecFromJson(j["positionMm"]);
	d.momentNAm = vecFromJson(j["momentNAm"]);
	d.latencyMs = j["latencyMs"].asDouble();
	d.goodnessOfFit = j["goodnessOfFit"].asDouble();
	d.clusterLabel = j["clusterLabel"].asInt();
	return d;
}

// MESIResultRecord -----------------------------------------------------------

const SourceCluster* MESIResultRecord::strongestCluster() const noexcept
{
	const SourceCluster* best = nullptr;
	for (const auto& c : clusters)
		if (!best || c.peakValue > best->peakValue) best = &c;
	return best;
}

void MESIResultRecord::setVerification(VerificationStatus status, std::string reviewer, std::string notes)
{
	verification = status;
	verifiedBy = std::move(reviewer);
	verificationNotes = std::move(notes);
	verifiedUtc = status == VerificationStatus::Unverified ? std::string() : nowIso8601();
}

JsonValue MESIResultRecord::toJson() const
{
	JsonValue clustersJson = JsonValue::array();
	for (const auto& c : clusters) clustersJson.push(c.toJson());
	JsonValue dipolesJson = JsonValue::array();
	for (const auto& d : dipoles) dipolesJson.push(d.toJson());

	return JsonValue::object()
		.set("resultId", resultId)
		.set("subjectId", subjectId)
		.set("modality", toString(modality))
		.set("method", method)
		.set("aiModel", aiModel)
		.set("sourceMapPath", sourceMapPath)
		.set("createdUtc", createdUtc)
		.set("computeSeconds", computeSeconds)
		.set("clusters", std::move(clustersJson))
		.set("dipoles", std::move(dipolesJson))
		.set("verification", JsonValue::object()
			.set("status", toString(verification))
			.set("verifiedBy", verifiedBy)
			.set("verifiedUtc", verifiedUtc)
			.set("notes", verificationNotes));
}

MESIResultRecord MESIResultRecord::fromJson(const JsonValue& j)
{
	MESIResultRecord r;
	r.resultId = j["resultId"].asString();
	r.subjectId = j["subjectId"].asString();
	r.modality = modalityFromString(j["modality"].asString()).value_or(Modality::None);
	r.method = j["method"].asString(r.method);
	r.aiModel = j["aiModel"].asString();
	r.sourceMapPath = j["sourceMapPath"].asString();
	r.createdUtc = j["createdUtc"].asString(r.createdUtc);
	r.computeSeconds = j["computeSeconds"].asDouble();
	if (j["clusters"].isArray())
		for (const auto& c : j["clusters"].asArray()) r.clusters.push_back(SourceCluster::fromJson(c));
	if (j["dipoles"].isArray())
		for (const auto& d : j["dipoles"].asArray()) r.dipoles.push_back(DipoleMoment::fromJson(d));
	const JsonValue& v = j["verification"];
	r.verification = verificationFromString(v["status"].asString()).value_or(VerificationStatus::Unverified);
	r.verifiedBy = v["verifiedBy"].asString();
	r.verifiedUtc = v["verifiedUtc"].asString();
	r.verificationNotes = v["notes"].asString();
	return r;
}

XmlNode MESIResultRecord::toXml() const
{
	XmlNode n("MESIResult");
	n.attr("id", resultId).attr("subjectId", subjectId).attr("modality", toString(modality))
	 .attr("method", method).attr("aiModel", aiModel).attr("createdUtc", createdUtc)
	 .attr("computeSeconds", computeSeconds).attr("verification", toString(verification))
	 .attr("verifiedBy", verifiedBy).attr("verifiedUtc", verifiedUtc);
	n.childText("SourceMapPath", sourceMapPath);
	n.childText("VerificationNotes", verificationNotes);
	XmlNode cl("Clusters");
	for (const auto& c : clusters) {
		XmlNode x("Cluster");
		x.attr("label", static_cast<long long>(c.label))
		 .attr("cx", c.centroidMm.x).attr("cy", c.centroidMm.y).attr("cz", c.centroidMm.z)
		 .attr("px", c.peakMm.x).attr("py", c.peakMm.y).attr("pz", c.peakMm.z)
		 .attr("peakValue", c.peakValue).attr("volumeMm3", c.volumeMm3)
		 .attr("voxelCount", static_cast<long long>(c.voxelCount)).attr("confidence", c.confidence)
		 .attr("anatomicalLabel", c.anatomicalLabel).attr("functionalLabel", c.functionalLabel);
		cl.child(std::move(x));
	}
	n.child(std::move(cl));
	XmlNode dp("Dipoles");
	for (const auto& d : dipoles) {
		XmlNode x("Dipole");
		x.attr("x", d.positionMm.x).attr("y", d.positionMm.y).attr("z", d.positionMm.z)
		 .attr("qx", d.momentNAm.x).attr("qy", d.momentNAm.y).attr("qz", d.momentNAm.z)
		 .attr("latencyMs", d.latencyMs).attr("gof", d.goodnessOfFit)
		 .attr("clusterLabel", static_cast<long long>(d.clusterLabel));
		dp.child(std::move(x));
	}
	n.child(std::move(dp));
	return n;
}

std::string MESIResultRecord::csvHeader()
{
	return "resultId,subjectId,modality,method,aiModel,createdUtc,verification,kind,label,"
		"x_mm,y_mm,z_mm,peak_x_mm,peak_y_mm,peak_z_mm,value,volume_mm3,voxels,confidence,"
		"qx_nAm,qy_nAm,qz_nAm,latency_ms,gof,anatomicalLabel,functionalLabel\n";
}

void MESIResultRecord::appendCsvRows(std::string& out) const
{
	const std::string prefix = csvQuote(resultId) + "," + csvQuote(subjectId) + "," + toString(modality) + "," +
		csvQuote(method) + "," + csvQuote(aiModel) + "," + createdUtc + "," + toString(verification) + ",";
	for (const auto& c : clusters) {
		out += prefix + "cluster," + std::to_string(c.label) + "," +
			fmt(c.centroidMm.x) + "," + fmt(c.centroidMm.y) + "," + fmt(c.centroidMm.z) + "," +
			fmt(c.peakMm.x) + "," + fmt(c.peakMm.y) + "," + fmt(c.peakMm.z) + "," +
			fmt(c.peakValue) + "," + fmt(c.volumeMm3) + "," + std::to_string(c.voxelCount) + "," + fmt(c.confidence) +
			",,,,,," + csvQuote(c.anatomicalLabel) + "," + csvQuote(c.functionalLabel) + "\n";
	}
	for (const auto& d : dipoles) {
		out += prefix + "dipole," + std::to_string(d.clusterLabel) + "," +
			fmt(d.positionMm.x) + "," + fmt(d.positionMm.y) + "," + fmt(d.positionMm.z) +
			",,,," + fmt(d.magnitudeNAm()) + ",,,," +
			fmt(d.momentNAm.x) + "," + fmt(d.momentNAm.y) + "," + fmt(d.momentNAm.z) + "," +
			fmt(d.latencyMs) + "," + fmt(d.goodnessOfFit) + ",,\n";
	}
}

} // namespace mesi
