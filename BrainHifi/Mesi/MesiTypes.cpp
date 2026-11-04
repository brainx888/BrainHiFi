#include "MesiTypes.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <utility>

namespace mesi {

namespace {

template <typename E, std::size_t N>
std::string enumToString(E value, const std::array<std::pair<E, const char*>, N>& table, const char* fallback)
{
	for (const auto& [e, name] : table)
		if (e == value) return name;
	return fallback;
}

template <typename E, std::size_t N>
std::optional<E> enumFromString(std::string_view s, const std::array<std::pair<E, const char*>, N>& table)
{
	for (const auto& [e, name] : table)
		if (s == name) return e;
	return std::nullopt;
}

constexpr std::array<std::pair<Modality, const char*>, 4> kModalityNames{ {
	{ Modality::None,             "None" },
	{ Modality::MEG,              "MEG" },
	{ Modality::EEG,              "EEG" },
	{ Modality::Combined_MEG_EEG, "Combined_MEG_EEG" },
} };

constexpr std::array<std::pair<CohortGroup, const char*>, 3> kCohortNames{ {
	{ CohortGroup::Unknown,   "Unknown" },
	{ CohortGroup::Pediatric, "Pediatric" },
	{ CohortGroup::Adult,     "Adult" },
} };

constexpr std::array<std::pair<ProcessingState, const char*>, 8> kStateNames{ {
	{ ProcessingState::Idle,       "Idle" },
	{ ProcessingState::Queued,     "Queued" },
	{ ProcessingState::Segmenting, "Segmenting" },
	{ ProcessingState::Segmented,  "Segmented" },
	{ ProcessingState::Fusing,     "Fusing" },
	{ ProcessingState::Completed,  "Completed" },
	{ ProcessingState::Failed,     "Failed" },
	{ ProcessingState::Cancelled,  "Cancelled" },
} };

constexpr std::array<std::pair<VerificationStatus, const char*>, 4> kVerificationNames{ {
	{ VerificationStatus::Unverified,     "Unverified" },
	{ VerificationStatus::AutoVerified,   "AutoVerified" },
	{ VerificationStatus::ExpertVerified, "ExpertVerified" },
	{ VerificationStatus::Rejected,       "Rejected" },
} };

constexpr std::array<std::pair<RuntimeType, const char*>, 6> kRuntimeNames{ {
	{ RuntimeType::CPU,           "CPU" },
	{ RuntimeType::ONNX_CPU,      "ONNX_CPU" },
	{ RuntimeType::ONNX_CUDA,     "ONNX_CUDA" },
	{ RuntimeType::ONNX_DirectML, "ONNX_DirectML" },
	{ RuntimeType::TensorRT,      "TensorRT" },
	{ RuntimeType::Remote,        "Remote" },
} };

} // namespace

std::string toString(Modality m)            { return enumToString(m, kModalityNames, "None"); }
std::string toString(CohortGroup g)         { return enumToString(g, kCohortNames, "Unknown"); }
std::string toString(ProcessingState s)     { return enumToString(s, kStateNames, "Idle"); }
std::string toString(VerificationStatus v)  { return enumToString(v, kVerificationNames, "Unverified"); }
std::string toString(RuntimeType r)         { return enumToString(r, kRuntimeNames, "CPU"); }

std::optional<Modality> modalityFromString(std::string_view s)
{
	if (s == "MEG+EEG" || s == "MEG|EEG") return Modality::Combined_MEG_EEG;
	return enumFromString(s, kModalityNames);
}
std::optional<CohortGroup>        cohortFromString(std::string_view s)          { return enumFromString(s, kCohortNames); }
std::optional<ProcessingState>    processingStateFromString(std::string_view s) { return enumFromString(s, kStateNames); }
std::optional<VerificationStatus> verificationFromString(std::string_view s)    { return enumFromString(s, kVerificationNames); }
std::optional<RuntimeType>        runtimeFromString(std::string_view s)         { return enumFromString(s, kRuntimeNames); }

std::string toIso8601(TimePoint tp)
{
	const std::time_t t = Clock::to_time_t(tp);
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[64];
	std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	return buf;
}

std::optional<TimePoint> fromIso8601(std::string_view s)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
	if (s.size() < 19) return std::nullopt;
	const std::string str(s.substr(0, 19));
	//if (std::sscanf_s(str.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6)
	//	return std::nullopt;
	if (std::sscanf(str.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6)
		return std::nullopt;
	std::tm tm{};
	tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
	tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
#if defined(_WIN32)
	const std::time_t t = _mkgmtime(&tm);
#else
	const std::time_t t = timegm(&tm);
#endif
	if (t == static_cast<std::time_t>(-1)) return std::nullopt;
	return Clock::from_time_t(t);
}

std::string nowIso8601() { return toIso8601(Clock::now()); }

} // namespace mesi
