#pragma once
// Playback helpers without Qt: the per-script offset, script time, the video and
// the axis files that belong to a script.
// Go reference: cmd/gui-wails/app_playback.go
// (SetScriptOffset, ReportVideoPosition), app.go (findMatchingVideo).
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace samn {

constexpr int64_t OFFSET_LIMIT_MS = 10000;

std::string offset_key(const std::string& script_path);
int64_t clamp_offset(int64_t ms);
// The script time shown at video_ms (ms - offset, not below 0).
int64_t script_time(int64_t video_ms, int64_t offset_ms);
// A video with the script's name beside it (findMatchingVideo).
std::optional<std::string> find_matching_video(const std::string& script_path);
// 0.7 companion axis scripts beside a script: <stem>.surge.funscript and
// so on, in the order surge, sway, twist, roll, pitch; only those that exist.
std::vector<std::pair<std::string, std::string>> companion_axis_files(const std::string& script_path);

// Multi-axis companion names in 0.7 order (CreateScriptAxis / "+ Axis…").
inline constexpr const char* kCompanionAxes[] = {"surge", "sway", "twist", "roll", "pitch"};

[[nodiscard]] bool is_companion_axis(const std::string& axis);

// Path of <stem>.<axis>.funscript beside script_path (whether or not it exists).
[[nodiscard]] std::string companion_axis_path(const std::string& script_path, const std::string& axis);

// CreateScriptAxis: write a new companion funscript centred at pos 50 over
// [0, duration_ms] (at least 1 ms). Never overwrites an existing file (D-086).
// Returns the created path, or nullopt with a short reason in `error`.
std::optional<std::string> create_companion_axis(const std::string& script_path, const std::string& axis,
                                                 int64_t duration_ms, std::string* error = nullptr);

}  // namespace samn
