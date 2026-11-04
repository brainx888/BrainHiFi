// MesiTypes.h - fundamental enumerations and small value types shared by the
// Multimodal Source Imaging (MESI) management system.
//
// This header is MFC-free (plain C++17) so it can be compiled into the
// background inference pipeline, unit tests and the MFC UI alike.
#pragma once

#include <cstdint>
#include <cmath>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace mesi {

// ---------------------------------------------------------------------------
// Modality flags
// ---------------------------------------------------------------------------
enum class Modality : std::uint32_t
{
	None             = 0,
	MEG              = 1u << 0,
	EEG              = 1u << 1,
	Combined_MEG_EEG = MEG | EEG,
};

constexpr Modality operator|(Modality a, Modality b) noexcept
{
	return static_cast<Modality>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr Modality operator&(Modality a, Modality b) noexcept
{
	return static_cast<Modality>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr Modality operator~(Modality a) noexcept
{
	return static_cast<Modality>(~static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(Modality::Combined_MEG_EEG));
}
inline Modality& operator|=(Modality& a, Modality b) noexcept { return a = a | b; }
inline Modality& operator&=(Modality& a, Modality b) noexcept { return a = a & b; }

constexpr bool hasModality(Modality set, Modality flag) noexcept
{
	return (set & flag) == flag && flag != Modality::None;
}
constexpr bool hasAnyModality(Modality set, Modality flags) noexcept
{
	return (set & flags) != Modality::None;
}

// ---------------------------------------------------------------------------
// Cohort / processing / verification enumerations
// ---------------------------------------------------------------------------
enum class CohortGroup : std::uint8_t
{
	Unknown = 0,
	Pediatric,   // < kAdultAgeYears
	Adult,
};

// Age boundary (years) between the pediatric and adult cohorts.
constexpr double kAdultAgeYears = 18.0;
// Upper bound used for validation of age input.
constexpr double kMaxAgeYears = 120.0;

constexpr CohortGroup cohortFromAge(double ageYears) noexcept
{
	if (ageYears < 0.0 || ageYears > kMaxAgeYears) return CohortGroup::Unknown;
	return ageYears < kAdultAgeYears ? CohortGroup::Pediatric : CohortGroup::Adult;
}

enum class ProcessingState : std::uint8_t
{
	Idle = 0,
	Queued,
	Segmenting,
	Segmented,     // AI segmentation finished, fusion not yet run
	Fusing,
	Completed,
	Failed,
	Cancelled,
};

constexpr bool isBusyState(ProcessingState s) noexcept
{
	return s == ProcessingState::Queued || s == ProcessingState::Segmenting || s == ProcessingState::Fusing;
}

enum class VerificationStatus : std::uint8_t
{
	Unverified = 0,
	AutoVerified,    // passed automatic plausibility checks
	ExpertVerified,  // signed off by a reviewer
	Rejected,
};

// Inference runtime of an AI segmentation model.
enum class RuntimeType : std::uint8_t
{
	CPU = 0,
	ONNX_CPU,
	ONNX_CUDA,
	ONNX_DirectML,
	TensorRT,
	Remote,          // cloud / HTTP inference service (e.g. SAM 3.x API)
};

// ---------------------------------------------------------------------------
// String conversions (stable identifiers used in JSON / XML files)
// ---------------------------------------------------------------------------
std::string toString(Modality m);
std::string toString(CohortGroup g);
std::string toString(ProcessingState s);
std::string toString(VerificationStatus v);
std::string toString(RuntimeType r);

std::optional<Modality>           modalityFromString(std::string_view s);
std::optional<CohortGroup>        cohortFromString(std::string_view s);
std::optional<ProcessingState>    processingStateFromString(std::string_view s);
std::optional<VerificationStatus> verificationFromString(std::string_view s);
std::optional<RuntimeType>        runtimeFromString(std::string_view s);

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct Vec3
{
	double x = 0.0, y = 0.0, z = 0.0;

	constexpr Vec3() = default;
	constexpr Vec3(double x_, double y_, double z_) noexcept : x(x_), y(y_), z(z_) {}

	constexpr Vec3 operator+(const Vec3& o) const noexcept { return { x + o.x, y + o.y, z + o.z }; }
	constexpr Vec3 operator-(const Vec3& o) const noexcept { return { x - o.x, y - o.y, z - o.z }; }
	constexpr Vec3 operator*(double k) const noexcept { return { x * k, y * k, z * k }; }
	constexpr double dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
	double norm() const noexcept { return std::sqrt(dot(*this)); }
};

inline bool nearlyEqual(const Vec3& a, const Vec3& b, double tol = 1e-9) noexcept
{
	return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol;
}

// Axis-aligned bounding box.  In image space the unit is voxels, in head
// space it is millimetres; the owner decides and documents which.
struct BoundingBox3D
{
	Vec3 minCorner;
	Vec3 maxCorner;

	bool isValid() const noexcept
	{
		return minCorner.x <= maxCorner.x && minCorner.y <= maxCorner.y && minCorner.z <= maxCorner.z;
	}
	Vec3 center() const noexcept { return (minCorner + maxCorner) * 0.5; }
	Vec3 extent() const noexcept { return maxCorner - minCorner; }
};

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;

// ISO-8601 UTC "YYYY-MM-DDTHH:MM:SSZ".
std::string toIso8601(TimePoint tp);
std::optional<TimePoint> fromIso8601(std::string_view s);
std::string nowIso8601();

} // namespace mesi
