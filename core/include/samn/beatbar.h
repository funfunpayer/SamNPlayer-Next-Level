#pragma once
#include <vector>
#include <cstdint>
#include <memory>
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

// BeatTimes one frame at a time, fed only the bar's rows (y0..y1 cropped),
// so a long video never sits in memory (0.7 streamBeatTimes). Same beats as
// BeatTimes on the full frames.
class BeatTimer {
public:
    BeatTimer(const BeatBar& bar, double fps);
    ~BeatTimer();
    BeatTimer(const BeatTimer&) = delete;
    BeatTimer& operator=(const BeatTimer&) = delete;
    void AddStrip(const GrayFrame& strip);
    [[nodiscard]] const std::vector<double>& Beats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 60 000 / median beat interval, rounded; 0 with fewer than 3 beats
// (0.7 medianBPM, display only).
double MedianBpm(const std::vector<double>& beatsMs);

}  // namespace samn
