#include "MesiEngines.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace mesi {

namespace {

// Small deterministic hash so simulated outputs are reproducible per subject.
std::uint32_t fnv1a(const std::string& s)
{
	std::uint32_t h = 2166136261u;
	for (const unsigned char c : s) { h ^= c; h *= 16777619u; }
	return h;
}

void pause(int ms, const CancellationToken& cancel)
{
	if (ms <= 0) { cancel.throwIfCancelled(); return; }
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (std::chrono::steady_clock::now() < deadline) {
		cancel.throwIfCancelled();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

} // namespace

SegmentationOutput SimulatedSegmentationEngine::run(const SegmentationInput& in, const CancellationToken& cancel, const ProgressFn& progress)
{
	const auto t0 = std::chrono::steady_clock::now();
	const std::uint32_t seed = fnv1a(in.subject.subjectId);
	const int nInstances = (std::min)(in.config.maxInstances, 1 + static_cast<int>(seed % 4));

	SegmentationOutput out;
	out.imageWidth = out.imageHeight = out.imageDepth = 256;
	for (int i = 0; i < nInstances; ++i) {
		if (progress) progress(10 + 80 * i / nInstances, "Segmenting cluster " + std::to_string(i + 1));
		pause(m_stepDelayMs, cancel);
		SegmentedInstance inst;
		inst.label = i + 1;
		inst.confidence = 0.55 + 0.4 * (((seed >> (i * 3)) & 7) / 7.0);
		if (inst.confidence < in.config.confidenceThreshold) continue;
		const double cx = 96.0 + ((seed >> (i * 5)) % 64), cy = 100.0 + ((seed >> (i * 7)) % 48), cz = 110.0 + ((seed >> (i * 2)) % 40);
		inst.boundingBoxVoxels.minCorner = { cx - 6, cy - 6, cz - 6 };
		inst.boundingBoxVoxels.maxCorner = { cx + 6, cy + 6, cz + 6 };
		inst.voxelCount = 400 + static_cast<int>((seed >> i) % 900);
		inst.className = "activation cluster";
		out.instances.push_back(inst);
	}
	out.maskPath = in.outputDirectory.empty() ? std::string() : in.outputDirectory + "/" + in.subject.subjectId + "_segmentation.sim";
	out.log = name() + ": " + std::to_string(out.instances.size()) + " instance(s) >= threshold " + std::to_string(in.config.confidenceThreshold);
	out.computeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	if (progress) progress(100, "Segmentation done");
	return out;
}

MESIResultRecord SimulatedFusionEngine::compute(const FusionInput& in, const CancellationToken& cancel, const ProgressFn& progress)
{
	const auto t0 = std::chrono::steady_clock::now();
	const std::uint32_t seed = fnv1a(in.subject.subjectId + "/fusion");
	MESIResultRecord r;
	r.method = name();
	r.modality = in.subject.modality.modalities;

	// One cluster per segmented instance, or a single default cluster when the
	// fusion runs without a prior segmentation.
	std::vector<SegmentedInstance> seeds = in.segmentation;
	if (seeds.empty()) {
		SegmentedInstance s;
		s.label = 1; s.confidence = 0.5;
		s.boundingBoxVoxels.minCorner = { 120, 120, 120 };
		s.boundingBoxVoxels.maxCorner = { 136, 136, 136 };
		s.voxelCount = 512;
		seeds.push_back(s);
	}

	const double voxelMm = 1.0;
	for (std::size_t i = 0; i < seeds.size(); ++i) {
		if (progress) progress(static_cast<int>(10 + 80 * i / seeds.size()), "Fusing cluster " + std::to_string(i + 1));
		pause(m_stepDelayMs, cancel);
		const SegmentedInstance& s = seeds[i];
		SourceCluster c;
		c.label = s.label;
		// voxel -> head mm: centre the 256^3 volume on the origin.
		c.centroidMm = (s.boundingBoxVoxels.center() - Vec3(128, 128, 128)) * voxelMm;
		c.peakMm = c.centroidMm + Vec3(1.0, -0.5, 0.5);
		c.peakValue = 2.0 + ((seed >> i) % 100) / 20.0;
		c.voxelCount = s.voxelCount;
		c.volumeMm3 = s.voxelCount * voxelMm * voxelMm * voxelMm;
		c.confidence = s.confidence;
		c.anatomicalLabel = s.className;
		r.clusters.push_back(c);

		DipoleMoment d;
		d.positionMm = c.peakMm;
		d.momentNAm = Vec3(10.0, 5.0, -2.0) * (c.peakValue / 5.0);
		d.latencyMs = 20.0 + 5.0 * static_cast<double>(i);
		d.goodnessOfFit = 0.85 + 0.1 * c.confidence;
		d.clusterLabel = c.label;
		r.dipoles.push_back(d);
	}
	r.sourceMapPath = in.outputDirectory.empty() ? std::string() : in.outputDirectory + "/" + in.subject.subjectId + "_mesi.sim";
	r.computeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	if (progress) progress(100, "Fusion done");
	return r;
}

} // namespace mesi
