#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <set>

#include "samn/beat_strokes.h"

using samn::Action;
using samn::BeatStrokeOptions;
using samn::BeatStrokes;

// 0.7 funscript/beat_strokes_test.go, both cases. Until now only the
// Python bindings ran them, so ctest did not cover beat strokes.

TEST_CASE("beat strokes: down on every beat, full stroke between slow beats") {
    const std::vector<double> beats{1000, 1500, 2000, 2500};
    const auto out = BeatStrokes(beats, BeatStrokeOptions{});
    std::set<int64_t> bottoms;
    for (const auto& a : out) {
        if (a.pos == 10) bottoms.insert(a.at);
    }
    for (double b : beats) {
        CHECK_MESSAGE(bottoms.count(static_cast<int64_t>(b)) == 1, "no bottom at beat " << b);
    }
    // 250 ms up at 500/s = 125 units, capped at top 90.
    bool sawMid = false;
    for (const auto& a : out) {
        if (a.at == 1250) {
            sawMid = true;
            CHECK(a.pos == 90);
        }
    }
    CHECK(sawMid);
}

TEST_CASE("beat strokes: speed limit holds and a long pause rises once and holds") {
    BeatStrokeOptions o;
    o.maxSpeed = 400;
    const auto out = BeatStrokes({0, 100, 200, 5200}, o);
    REQUIRE(out.size() > 1);
    for (size_t i = 1; i < out.size(); ++i) {
        const double dt = (out[i].at - out[i - 1].at) / 1000.0;
        CHECK(std::abs(out[i].pos - out[i - 1].pos) <= 400 * dt + 1);
        CHECK(out[i].at > out[i - 1].at);
    }
    int held = 0;
    for (const auto& a : out) {
        if (a.at > 200 && a.at < 5200) ++held;
    }
    CHECK(held == 2);
    CHECK(BeatStrokes({}, BeatStrokeOptions{}).empty());
}
