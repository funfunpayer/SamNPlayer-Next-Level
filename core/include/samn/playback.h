#pragma once
// Playback session logic without Qt: offset, loop, position on the curve.
// Port of samn/player/session.py. Go reference: cmd/gui-wails/app_playback.go
// (SetScriptOffset, ReportVideoPosition), app.go (findMatchingVideo).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "samn/funscript.h"
#include "samn/settings.h"

namespace samn {

constexpr int64_t OFFSET_LIMIT_MS = 10000;

std::string offset_key(const std::string& script_path);
int64_t clamp_offset(int64_t ms);
// The script time shown at video_ms (ms - offset, not below 0).
int64_t script_time(int64_t video_ms, int64_t offset_ms);
// A video with the script's name beside it (findMatchingVideo).
std::optional<std::string> find_matching_video(const std::string& script_path);
// Interpolated position (0-100) at t_ms; nullopt without points.
std::optional<double> position_at(const std::vector<Action>& actions, int64_t t_ms);

struct LoopRange {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    // Where to seek when video_ms has reached the end; nullopt to keep playing.
    std::optional<int64_t> jump(int64_t video_ms) const;
};

struct Session {
    explicit Session(Settings& s) : settings(s) {}

    Settings& settings;
    std::string script_path;
    std::string video_path;
    int64_t offset_ms = 0;
    std::optional<LoopRange> loop;
    std::vector<Action> actions;

    // video: nullopt looks for a matching video beside the script; "" means none.
    void open_script(const std::string& path, std::vector<Action> acts,
                     const std::optional<std::string>& video = std::nullopt);
    int64_t set_offset(int64_t ms);
    int64_t script_ms(int64_t video_ms) const { return script_time(video_ms, offset_ms); }
};

}  // namespace samn
