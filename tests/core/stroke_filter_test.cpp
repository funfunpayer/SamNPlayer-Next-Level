#include <doctest/doctest.h>

#include <cmath>
#include <map>

#include "samn/stroke_filter.h"

using samn::Action;
using samn::ApplyStrokeFilter;
using samn::StrokeFilter;

// 0.7 funscript/stroke_filter_test.go, all cases, plus the checks the
// Python parity tests did not make.
namespace {
const std::vector<Action> kZigzag{{0, 0}, {500, 100}, {1000, 0}, {1500, 100}, {2000, 0}};

StrokeFilter Smooth(const char* mode, int64_t step) {
    StrokeFilter f;
    f.smooth = mode;
    f.stepMs = step;
    return f;
}
}  // namespace

TEST_CASE("Stroke filter off changes nothing and leaves the input alone") {
    const std::vector<Action> in = kZigzag;
    auto out = ApplyStrokeFilter(in, StrokeFilter{});
    REQUIRE(out.size() == kZigzag.size());
    out[0].pos = 55;
    CHECK(in[0].pos == 0);
    CHECK_FALSE(samn::Active(StrokeFilter{}));
}

TEST_CASE("Smooth keeps the points, stays in range and rounds the top") {
    for (const char* mode : {"pchip", "makima"}) {
        CAPTURE(mode);
        const auto out = ApplyStrokeFilter(kZigzag, Smooth(mode, 50));
        REQUIRE(out.size() > kZigzag.size());
        std::map<int64_t, int> orig;
        for (const auto& a : kZigzag) orig[a.at] = a.pos;
        int at600 = -1;
        for (size_t i = 0; i < out.size(); ++i) {
            CHECK(out[i].pos >= 0);
            CHECK(out[i].pos <= 100);
            if (i > 0) CHECK(out[i].at > out[i - 1].at);
            if (auto it = orig.find(out[i].at); it != orig.end()) CHECK(out[i].pos == it->second);
            if (out[i].at == 600) at600 = out[i].pos;
        }
        // Leaving the top at 500 ms flat: 100 ms later still above the straight line's 80.
        CHECK(at600 > 80);
    }
}

TEST_CASE("PCHIP does not overshoot monotone data") {
    const std::vector<Action> a{{0, 0}, {300, 10}, {600, 90}, {900, 100}};
    const auto out = ApplyStrokeFilter(a, Smooth("pchip", 25));
    for (size_t i = 1; i < out.size(); ++i) CHECK(out[i].pos >= out[i - 1].pos);
}

TEST_CASE("Speed limit stops short, then catches up") {
    StrokeFilter f;
    f.maxSpeed = 400;
    const auto out = ApplyStrokeFilter({{0, 0}, {100, 100}, {1000, 100}}, f);
    REQUIRE(out.size() == 3);
    CHECK(out[1].pos == 40);  // 100 ms at 400/s
    CHECK(out[2].pos == 100);
    for (size_t i = 1; i < out.size(); ++i) {
        const double dt = double(out[i].at - out[i - 1].at) / 1000;
        CHECK(std::abs(double(out[i].pos - out[i - 1].pos)) <= 400 * dt + 1e-9);
    }
}

TEST_CASE("Idle fill waves gently in long pauses only") {
    StrokeFilter f;
    f.idleFillMs = 4000;
    const auto out = ApplyStrokeFilter({{0, 50}, {10000, 50}, {10500, 0}}, f);
    REQUIRE(out.size() >= 8);
    for (size_t i = 1; i + 2 < out.size(); ++i) {
        CHECK(out[i].at > 0);
        CHECK(out[i].at < 10000);
        CHECK(out[i].pos >= 50 - samn::kIdleWaveAmp);
        CHECK(out[i].pos <= 50 + samn::kIdleWaveAmp);
    }
    CHECK(ApplyStrokeFilter({{0, 0}, {1000, 100}}, f).size() == 2);  // a short gap stays
}

TEST_CASE("NormalizeSmooth") {
    CHECK(samn::NormalizeSmooth("pchip") == "pchip");
    CHECK(samn::NormalizeSmooth("makima") == "makima");
    CHECK(samn::NormalizeSmooth("cubic").empty());
    CHECK(samn::NormalizeSmooth("").empty());
}

TEST_CASE("Unsorted input is sorted and the last duplicate time kept") {
    StrokeFilter f;
    f.maxSpeed = 1000;
    const auto out = ApplyStrokeFilter({{1000, 10}, {0, 0}, {500, 50}, {500, 80}, {1000, 100}}, f);
    REQUIRE(out.size() == 3);
    CHECK(out[0].at == 0);
    CHECK(out[1].at == 500);
    CHECK(out[1].pos == 80);
    CHECK(out[2].at == 1000);
    CHECK(out[2].pos == 100);
}
