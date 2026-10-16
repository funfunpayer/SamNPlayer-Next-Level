#pragma once
#include <vector>
#include <cstdint>
#include <optional>

namespace samn {

struct GrayFrame {
    int w, h;
    std::vector<uint8_t> pix;
};

struct BeatBar {
    int y0, y1;
    double velocity;
    double target_x;
    double score;
};

// Port of generator.FindBeatBar
std::optional<BeatBar> FindBeatBar(const std::vector<GrayFrame>& frames);

// Port of generator.BeatTimes
std::vector<double> BeatTimes(const std::vector<GrayFrame>& frames, const BeatBar& bar, double fps);

}  // namespace samn
