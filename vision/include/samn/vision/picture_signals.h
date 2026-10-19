// What the picture does over the clip, for the phase rules (D-157): mean
// brightness and mean change per step, both 0..1. Small grey frames, so it
// is cheap; the decode itself is the cost.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace samn::vision {

struct PictureSignals {
    int64_t step_ms = 500;
    std::vector<double> luma;
    std::vector<double> motion;
};

struct PictureSignalOptions {
    int64_t step_ms = 500;
    std::function<bool()> cancel;           // true stops the run ("canceled")
    std::function<void(int)> progress;      // 0..100
};

// Empty result and *error set when the video cannot be read or the run was
// canceled.
PictureSignals MeasurePictureSignals(const std::filesystem::path& video, const PictureSignalOptions& o,
                                     std::string* error);

}  // namespace samn::vision
