#pragma once
// Beat bar from a video file (0.7 generator/beatbar_video.go; the detection
// itself is samn/beatbar.h): small grey frames at 30 fps. A first pass over
// a window finds the strip; a second pass over the whole video reads only
// the strip's rows and times every beat. The Editor's beat-bar tool (Cock
// Hero / Fap Hero style videos).
//
// Memory: the find window holds 90 s of 320 x 192 grey frames (about
// 166 MB, as in 0.7) and is freed before the second pass; the second pass
// keeps one strip at a time.
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/beatbar.h"

namespace samn::engine {

constexpr int kBeatBarFps = 30;
constexpr int kBeatBarW = 320;
constexpr int kBeatBarH = 192;  // 24 bands of 8 rows; the aspect does not matter here
constexpr double kBeatBarFindSec = 90;

struct BeatBarResult {
    bool found = false;
    BeatBar bar{};
    bool vertical = false;  // the strip runs up/down at a side edge
    std::vector<double> beats_ms;
    double bpm = 0;  // median beat rate, display only
};

struct BeatBarOptions {
    std::function<bool()> cancel;               // polled once per frame
    std::function<void(int percent)> progress;  // 0..99 while working
};

// nullopt with *error when the video cannot be read or the run was
// canceled ("canceled"); found == false when there is no beat bar.
std::optional<BeatBarResult> DetectBeatBarBeats(const std::filesystem::path& video, const BeatBarOptions& options,
                                                std::string* error);

}  // namespace samn::engine
