#pragma once
// Script Doctor: signal quality of a script from its points alone (0.7
// funscript/script_quality.go + device_compat.go). Jitter, jumps, gaps and
// device limits; NOT proof that the curve matches the video (that is motion
// fidelity). Same thresholds and score as Go; the warnings are in English
// (0.7 had them in German).
#include <filesystem>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// Community device limits (0.7 device_compat.go).
constexpr double kMinActionIntervalMs = 100.0;
constexpr double kSlowestFullStrokeMs = 900.0;
constexpr int kUsablePositionMin = 5;
constexpr int kUsablePositionMax = 95;

struct DeviceCompatMetrics {
    double avg_intensity = 0;   // 500 x |dpos| / dt, time-weighted, 0.1
    double peak_intensity = 0;  // 95th percentile, 0.1
    int actions_too_close = 0;
    double too_close_share = 0;
    int slow_full_strokes = 0;
    double position_span = 0;
    int actions_outside_usable_range = 0;
};

struct DeviceCompat {
    DeviceCompatMetrics metrics;
    std::vector<std::string> warnings;
};
// Actions in the given order (0.7 EvaluateDeviceCompat).
DeviceCompat EvaluateDeviceCompat(const std::vector<Action>& actions);

struct ScriptQualityResult {
    double score = 0;  // 0-1
    bool passed = false;  // score >= 0.5
    std::vector<std::string> warnings;
    bool estimated_from_script_only = true;
    std::string kind = "signal_quality";
};
// Actions in FILE order: unsorted timestamps are flagged before sorting.
ScriptQualityResult EvaluateScriptQuality(const std::vector<Action>& actions);

// Reads "actions" (or .samn "general") in file order and evaluates them.
// Throws std::runtime_error when the file cannot be read, is not JSON or
// has no actions (0.7 generator.ScriptQuality messages).
ScriptQualityResult ScriptQualityOfFile(const std::filesystem::path& path);

}  // namespace samn
