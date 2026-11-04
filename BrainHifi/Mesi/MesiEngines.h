// MesiEngines.h - extension points for the compute side of MESI.
//
//   IAISegmentationEngine : segments anatomy / activation clusters (ONNX,
//                           CUDA, TensorRT or a remote SAM-style service).
//   IMESIFusionEngine     : fuses MEG / EEG source estimates into one source
//                           map and extracts clusters / dipoles.
//
// Engines run on MESIManager worker threads.  They must be re-entrant with
// respect to different inputs or protect their own state (a CUDA context,
// for example, is typically bound to one worker; set workerThreads=1).
// Engines report progress through ProgressFn and must poll the
// CancellationToken at reasonable intervals.
#pragma once

#include "AISegmentationConfig.h"
#include "MESIResultRecord.h"
#include "SubjectRecord.h"

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mesi {

class CancellationToken
{
public:
	CancellationToken() : m_flag(std::make_shared<std::atomic<bool>>(false)) {}
	void cancel() noexcept { m_flag->store(true, std::memory_order_release); }
	bool isCancelled() const noexcept { return m_flag->load(std::memory_order_acquire); }
	// Convenience for engines: throw to unwind out of a deep compute loop.
	void throwIfCancelled() const { if (isCancelled()) throw OperationCancelled(); }

	struct OperationCancelled : std::runtime_error
	{
		OperationCancelled() : std::runtime_error("operation cancelled") {}
	};

private:
	std::shared_ptr<std::atomic<bool>> m_flag;
};

// percent in [0,100]; stage is a short human-readable label.
using ProgressFn = std::function<void(int percent, const std::string& stage)>;

// ---------------------------------------------------------------------------
// AI segmentation
// ---------------------------------------------------------------------------
struct SegmentedInstance
{
	int label = 0;
	double confidence = 0.0;
	BoundingBox3D boundingBoxVoxels;
	int voxelCount = 0;
	std::string className;           // free text from the model, may be empty
};

struct SegmentationInput
{
	SubjectRecord subject;           // snapshot; engine must not write back
	AISegmentationConfig config;
	std::string outputDirectory;     // where masks may be written (UTF-8)
};

struct SegmentationOutput
{
	std::string maskPath;            // label volume / mask written by the engine
	std::vector<SegmentedInstance> instances;
	int imageWidth = 0, imageHeight = 0, imageDepth = 0;
	double computeSeconds = 0.0;
	std::string log;                 // engine diagnostics for the UI
};

class IAISegmentationEngine
{
public:
	virtual ~IAISegmentationEngine() = default;
	virtual std::string name() const = 0;
	virtual bool supports(RuntimeType runtime) const = 0;
	// Throws std::exception on failure, CancellationToken::OperationCancelled on cancel.
	virtual SegmentationOutput run(const SegmentationInput& in, const CancellationToken& cancel, const ProgressFn& progress) = 0;
};

// ---------------------------------------------------------------------------
// MESI fusion
// ---------------------------------------------------------------------------
struct FusionInput
{
	SubjectRecord subject;
	AISegmentationConfig aiConfig;
	std::vector<SegmentedInstance> segmentation;   // from the last segmentation run (may be empty)
	std::string segmentationMaskPath;
	std::string outputDirectory;
};

class IMESIFusionEngine
{
public:
	virtual ~IMESIFusionEngine() = default;
	virtual std::string name() const = 0;
	// Fills clusters / dipoles / sourceMapPath / method.  resultId, subjectId,
	// timestamps and aiModel are set by MESIManager.
	virtual MESIResultRecord compute(const FusionInput& in, const CancellationToken& cancel, const ProgressFn& progress) = 0;
};

// ---------------------------------------------------------------------------
// Deterministic simulation engines.  They do no real inference; they let the
// whole management pipeline (threads, states, persistence, UI) be exercised
// without a GPU, model files or network.  Replace with real engines through
// MESIManager::setSegmentationEngine / setFusionEngine.
// ---------------------------------------------------------------------------
class SimulatedSegmentationEngine final : public IAISegmentationEngine
{
public:
	explicit SimulatedSegmentationEngine(int stepDelayMs = 0) : m_stepDelayMs(stepDelayMs) {}
	std::string name() const override { return "SimulatedSegmentation"; }
	bool supports(RuntimeType) const override { return true; }
	SegmentationOutput run(const SegmentationInput& in, const CancellationToken& cancel, const ProgressFn& progress) override;
private:
	int m_stepDelayMs;
};

class SimulatedFusionEngine final : public IMESIFusionEngine
{
public:
	explicit SimulatedFusionEngine(int stepDelayMs = 0) : m_stepDelayMs(stepDelayMs) {}
	std::string name() const override { return "SimulatedFusion"; }
	MESIResultRecord compute(const FusionInput& in, const CancellationToken& cancel, const ProgressFn& progress) override;
private:
	int m_stepDelayMs;
};

} // namespace mesi
