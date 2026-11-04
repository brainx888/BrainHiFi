// ModalityConfig.h - which functional modalities a subject was recorded with
// and where the co-registered anatomical volumes live.
#pragma once

#include "MesiTypes.h"
#include "MesiJson.h"
#include "MesiXml.h"

#include <string>
#include <vector>

namespace mesi {

struct ModalityConfig
{
	Modality modalities = Modality::None;

	// Functional recordings (UTF-8 paths; empty when not available).
	std::string megDataPath;
	std::string eegDataPath;
	double megSamplingRateHz = 0.0;
	double eegSamplingRateHz = 0.0;

	// Anatomy co-registered to the functional head coordinate frame.
	std::string coregisteredMriPath;     // T1 volume in head coordinates
	std::string coregistrationTransformPath; // 4x4 device->MRI transform (optional)
	std::string headModelPath;           // BEM / sphere model (optional)

	bool hasMEG() const noexcept { return hasModality(modalities, Modality::MEG); }
	bool hasEEG() const noexcept { return hasModality(modalities, Modality::EEG); }
	bool isCombined() const noexcept { return modalities == Modality::Combined_MEG_EEG; }

	// Human-readable "MEG", "EEG", "MEG+EEG" or "-".
	std::string label() const
	{
		if (isCombined()) return "MEG+EEG";
		if (hasMEG()) return "MEG";
		if (hasEEG()) return "EEG";
		return "-";
	}

	// Returns problems that make the configuration unusable for processing.
	std::vector<std::string> validate() const
	{
		std::vector<std::string> errors;
		if (modalities == Modality::None) errors.push_back("No functional modality selected.");
		if (hasMEG() && megDataPath.empty()) errors.push_back("MEG selected but no MEG data path.");
		if (hasEEG() && eegDataPath.empty()) errors.push_back("EEG selected but no EEG data path.");
		if (coregisteredMriPath.empty()) errors.push_back("No co-registered MRI volume.");
		return errors;
	}

	JsonValue toJson() const
	{
		return JsonValue::object()
			.set("modalities", toString(modalities))
			.set("megDataPath", megDataPath)
			.set("eegDataPath", eegDataPath)
			.set("megSamplingRateHz", megSamplingRateHz)
			.set("eegSamplingRateHz", eegSamplingRateHz)
			.set("coregisteredMriPath", coregisteredMriPath)
			.set("coregistrationTransformPath", coregistrationTransformPath)
			.set("headModelPath", headModelPath);
	}

	static ModalityConfig fromJson(const JsonValue& j)
	{
		ModalityConfig c;
		c.modalities = modalityFromString(j["modalities"].asString()).value_or(Modality::None);
		c.megDataPath = j["megDataPath"].asString();
		c.eegDataPath = j["eegDataPath"].asString();
		c.megSamplingRateHz = j["megSamplingRateHz"].asDouble();
		c.eegSamplingRateHz = j["eegSamplingRateHz"].asDouble();
		c.coregisteredMriPath = j["coregisteredMriPath"].asString();
		c.coregistrationTransformPath = j["coregistrationTransformPath"].asString();
		c.headModelPath = j["headModelPath"].asString();
		return c;
	}

	XmlNode toXml() const
	{
		XmlNode n("ModalityConfig");
		n.attr("modalities", toString(modalities))
		 .attr("megSamplingRateHz", megSamplingRateHz)
		 .attr("eegSamplingRateHz", eegSamplingRateHz);
		n.childText("MegDataPath", megDataPath)
		 .childText("EegDataPath", eegDataPath)
		 .childText("CoregisteredMriPath", coregisteredMriPath)
		 .childText("CoregistrationTransformPath", coregistrationTransformPath)
		 .childText("HeadModelPath", headModelPath);
		return n;
	}

	static ModalityConfig fromXml(const XmlNode& n)
	{
		ModalityConfig c;
		c.modalities = modalityFromString(n.attribute("modalities").value_or("None")).value_or(Modality::None);
		c.megSamplingRateHz = n.attributeDouble("megSamplingRateHz");
		c.eegSamplingRateHz = n.attributeDouble("eegSamplingRateHz");
		c.megDataPath = n.childText("MegDataPath");
		c.eegDataPath = n.childText("EegDataPath");
		c.coregisteredMriPath = n.childText("CoregisteredMriPath");
		c.coregistrationTransformPath = n.childText("CoregistrationTransformPath");
		c.headModelPath = n.childText("HeadModelPath");
		return c;
	}
};

} // namespace mesi
