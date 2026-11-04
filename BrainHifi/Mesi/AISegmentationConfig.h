// AISegmentationConfig.h - metadata and parameters of the AI model used to
// segment anatomy / activation clusters before MESI fusion.
#pragma once

#include "MesiTypes.h"
#include "MesiJson.h"

#include <optional>
#include <string>
#include <vector>

namespace mesi {

struct AISegmentationConfig
{
	// Model identity -------------------------------------------------------
	std::string modelName = "sam-3.1";
	std::string modelVersion = "1.0";
	RuntimeType runtime = RuntimeType::Remote;
	std::string modelPath;             // .onnx / .engine file; empty for Remote
	std::string endpoint = "api.meta.ai";  // host for RuntimeType::Remote

	// Prompting -------------------------------------------------------------
	std::string promptText = "the bright colored activation clusters overlaid on the brain";
	std::optional<BoundingBox3D> boundingBox;   // voxel-space box prompt (optional)

	// Thresholds / limits ---------------------------------------------------
	double confidenceThreshold = 0.50;   // [0,1] keep instances with score >= threshold
	double iouThreshold = 0.70;          // [0,1] non-maximum suppression overlap
	int maxInstances = 32;               // >= 1
	int deviceId = 0;                    // CUDA / DirectML adapter index
	int numThreads = 0;                  // 0 = runtime default
	bool useFp16 = false;

	bool isRemote() const noexcept { return runtime == RuntimeType::Remote; }
	bool isGpu() const noexcept
	{
		return runtime == RuntimeType::ONNX_CUDA || runtime == RuntimeType::ONNX_DirectML || runtime == RuntimeType::TensorRT;
	}

	friend bool operator==(const AISegmentationConfig& a, const AISegmentationConfig& b)
	{
		return a.modelName == b.modelName && a.modelVersion == b.modelVersion && a.runtime == b.runtime
			&& a.modelPath == b.modelPath && a.endpoint == b.endpoint && a.promptText == b.promptText
			&& a.boundingBox.has_value() == b.boundingBox.has_value()
			&& (!a.boundingBox || (nearlyEqual(a.boundingBox->minCorner, b.boundingBox->minCorner) && nearlyEqual(a.boundingBox->maxCorner, b.boundingBox->maxCorner)))
			&& a.confidenceThreshold == b.confidenceThreshold && a.iouThreshold == b.iouThreshold
			&& a.maxInstances == b.maxInstances && a.deviceId == b.deviceId && a.numThreads == b.numThreads && a.useFp16 == b.useFp16;
	}
	friend bool operator!=(const AISegmentationConfig& a, const AISegmentationConfig& b) { return !(a == b); }
	std::string displayName() const { return modelName + " v" + modelVersion + " (" + toString(runtime) + ")"; }

	std::vector<std::string> validate() const
	{
		std::vector<std::string> errors;
		if (modelName.empty()) errors.push_back("Model name is empty.");
		if (!isRemote() && modelPath.empty()) errors.push_back("Local runtime selected but model path is empty.");
		if (isRemote() && endpoint.empty()) errors.push_back("Remote runtime selected but endpoint is empty.");
		if (promptText.empty() && !boundingBox) errors.push_back("Neither a text prompt nor a bounding box is set.");
		if (confidenceThreshold < 0.0 || confidenceThreshold > 1.0) errors.push_back("Confidence threshold must be in [0,1].");
		if (iouThreshold < 0.0 || iouThreshold > 1.0) errors.push_back("IoU threshold must be in [0,1].");
		if (maxInstances < 1) errors.push_back("Max instances must be >= 1.");
		if (boundingBox && !boundingBox->isValid()) errors.push_back("Bounding box min corner exceeds max corner.");
		if (deviceId < 0) errors.push_back("Device id must be >= 0.");
		if (numThreads < 0) errors.push_back("Thread count must be >= 0.");
		return errors;
	}

	JsonValue toJson() const
	{
		JsonValue j = JsonValue::object()
			.set("modelName", modelName)
			.set("modelVersion", modelVersion)
			.set("runtime", toString(runtime))
			.set("modelPath", modelPath)
			.set("endpoint", endpoint)
			.set("promptText", promptText)
			.set("confidenceThreshold", confidenceThreshold)
			.set("iouThreshold", iouThreshold)
			.set("maxInstances", maxInstances)
			.set("deviceId", deviceId)
			.set("numThreads", numThreads)
			.set("useFp16", useFp16);
		if (boundingBox) {
			j.set("boundingBox", JsonValue::object()
				.set("min", JsonValue::Array{ boundingBox->minCorner.x, boundingBox->minCorner.y, boundingBox->minCorner.z })
				.set("max", JsonValue::Array{ boundingBox->maxCorner.x, boundingBox->maxCorner.y, boundingBox->maxCorner.z }));
		}
		return j;
	}

	static AISegmentationConfig fromJson(const JsonValue& j)
	{
		AISegmentationConfig c;
		c.modelName = j["modelName"].asString(c.modelName);
		c.modelVersion = j["modelVersion"].asString(c.modelVersion);
		c.runtime = runtimeFromString(j["runtime"].asString()).value_or(c.runtime);
		c.modelPath = j["modelPath"].asString();
		c.endpoint = j["endpoint"].asString();
		c.promptText = j["promptText"].asString(c.promptText);
		c.confidenceThreshold = j["confidenceThreshold"].asDouble(c.confidenceThreshold);
		c.iouThreshold = j["iouThreshold"].asDouble(c.iouThreshold);
		c.maxInstances = j["maxInstances"].asInt(c.maxInstances);
		c.deviceId = j["deviceId"].asInt(c.deviceId);
		c.numThreads = j["numThreads"].asInt(c.numThreads);
		c.useFp16 = j["useFp16"].asBool(c.useFp16);
		const JsonValue& bb = j["boundingBox"];
		if (bb.isObject() && bb["min"].size() == 3 && bb["max"].size() == 3) {
			BoundingBox3D box;
			box.minCorner = { bb["min"].at(0).asDouble(), bb["min"].at(1).asDouble(), bb["min"].at(2).asDouble() };
			box.maxCorner = { bb["max"].at(0).asDouble(), bb["max"].at(1).asDouble(), bb["max"].at(2).asDouble() };
			c.boundingBox = box;
		}
		return c;
	}
};

} // namespace mesi
