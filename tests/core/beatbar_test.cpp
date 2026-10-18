#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <random>

#include "samn/beatbar.h"

using samn::BeatTimes;
using samn::FindBeatBar;
using samn::GrayFrame;

// 0.7 generator/beatbar_test.go: the synthetic beat-bar cases and the
// plain-noise negative case. Until now only the Python bindings ran them.
namespace {

struct Synth {
    std::vector<GrayFrame> frames;
    std::vector<double> beats;  // true beat times in frames
};

// w×h frames of random "film" noise with a strip (rows y0..y1): dark
// background, a static target line at targetX, and dots that scroll at
// speed px/frame (negative = leftward), spawned every period frames and
// gone once they pass the target.
Synth SynthBeatVideo(int n, int w, int h, int y0, int y1, int targetX, double speed, int period,
                     unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> noise(0, 119);
    Synth s;
    std::vector<double> dots;
    std::vector<double> beats;
    const double start = speed < 0 ? double(w - 4) : 3.0;
    for (int i = 0; i < n; ++i) {
        GrayFrame f{w, h, std::vector<uint8_t>(size_t(w) * h)};
        for (auto& p : f.pix) p = uint8_t(noise(rng));
        if (i % period == 0) {
            dots.push_back(start);
            beats.push_back(i + std::abs(start - targetX) / std::abs(speed));
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) f.pix[size_t(y) * w + x] = 10;
            f.pix[size_t(y) * w + targetX] = 200;
        }
        std::vector<double> keep;
        for (double d : dots) {
            const bool passed = (speed < 0 && d < targetX) || (speed > 0 && d > targetX);
            if (passed) continue;
            const int cx = int(std::lround(d));
            for (int y = y0; y < y1; ++y) {
                for (int x = cx - 2; x <= cx + 2; ++x) {
                    if (x >= 0 && x < w) f.pix[size_t(y) * w + x] = 250;
                }
            }
            keep.push_back(d + speed);
        }
        dots = keep;
        s.frames.push_back(std::move(f));
    }
    for (double b : beats) {
        if (b < n - 1) s.beats.push_back(b);
    }
    return s;
}

void CheckCase(double speed, int targetX) {
    const auto s = SynthBeatVideo(300, 320, 192, 168, 176, targetX, speed, 17, 1);
    const auto bar = FindBeatBar(s.frames);
    REQUIRE(bar.has_value());
    CHECK(bar->y0 == 168);
    CHECK(bar->y1 == 176);
    CHECK(std::abs(bar->velocity - speed) <= 0.6);
    CHECK(std::abs(bar->target_x - targetX) <= 4);

    const auto beats = BeatTimes(s.frames, *bar, 30);
    const double frameMs = 1000.0 / 30;
    const auto truth = s.beats.size();
    CHECK(beats.size() + 1 >= truth);
    CHECK(beats.size() <= truth + 1);
    for (double tb : s.beats) {
        double best = std::numeric_limits<double>::infinity();
        for (double b : beats) best = std::min(best, std::abs(b - tb * frameMs));
        CHECK_MESSAGE(best <= 1.5 * frameMs, "beat at " << tb * frameMs << " ms missed by " << best);
    }
}

}  // namespace

TEST_CASE("beat bar: leftward dots to the left third") { CheckCase(-8, 60); }

TEST_CASE("beat bar: rightward dots to the middle") { CheckCase(6, 160); }

TEST_CASE("beat bar: plain noise is not taken for a beat bar") {
    std::mt19937 rng(2);
    std::uniform_int_distribution<int> noise(0, 199);
    std::vector<GrayFrame> frames;
    for (int i = 0; i < 120; ++i) {
        GrayFrame f{160, 96, std::vector<uint8_t>(160 * 96)};
        for (auto& p : f.pix) p = uint8_t(noise(rng));
        frames.push_back(std::move(f));
    }
    CHECK_FALSE(FindBeatBar(frames).has_value());
}

TEST_CASE("beat bar: the streaming BeatTimer gives the same beats from the strip alone") {
    const auto s = SynthBeatVideo(300, 320, 192, 168, 176, 60, -8, 17, 1);
    const auto bar = FindBeatBar(s.frames);
    REQUIRE(bar.has_value());
    const auto whole = BeatTimes(s.frames, *bar, 30);
    samn::BeatTimer timer(*bar, 30);
    for (const GrayFrame& f : s.frames) {
        GrayFrame strip{f.w, bar->y1 - bar->y0, {}};
        strip.pix.assign(f.pix.begin() + ptrdiff_t(bar->y0) * f.w, f.pix.begin() + ptrdiff_t(bar->y1) * f.w);
        timer.AddStrip(strip);
    }
    REQUIRE(!whole.empty());
    CHECK(timer.Beats() == whole);

    samn::BeatBar still = *bar;
    still.velocity = 0;
    samn::BeatTimer none(still, 30);
    none.AddStrip(s.frames[0]);
    CHECK(none.Beats().empty());
}

TEST_CASE("beat bar: median BPM (0.7 medianBPM)") {
    CHECK(samn::MedianBpm({0, 500}) == 0);
    CHECK(samn::MedianBpm({0, 500, 1000, 1500}) == 120);
    CHECK(samn::MedianBpm({0, 500, 1000, 1400, 1800}) == 133);  // median of 500,500,400,400 = 450
    CHECK(samn::MedianBpm({0, 0, 0}) == 0);
}
