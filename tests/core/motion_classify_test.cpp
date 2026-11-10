#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "samn/motion_classify.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<samn::MotionPoint> Sine(int n, double fps, double freq) {
    std::vector<samn::MotionPoint> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fps;
        out[static_cast<size_t>(i)] = {t * 1000.0, (std::sin(2 * kPi * freq * t) + 1) / 2 * 100};
    }
    return out;
}

}  // namespace

// 0.7 motionx/motionx_test.go TestClassifyStaticCurve.
TEST_CASE("Classify: a flat curve is one static segment") {
    std::vector<samn::MotionPoint> pts(300);
    for (int i = 0; i < 300; ++i) pts[static_cast<size_t>(i)] = {static_cast<double>(i) * 33, 50};
    const auto segs = samn::ClassifyMotion(pts, {});
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].state == samn::MotionState::Static);
}

// 0.7 TestClassifyStartStop. Empty options: smoothing stays off.
TEST_CASE("Classify: still, then motion, then still") {
    std::vector<samn::MotionPoint> pts;
    double t_ms = 0;
    for (int i = 0; i < 30; ++i) {
        pts.push_back({t_ms, 20});
        t_ms += 33;
    }
    for (const auto& p : Sine(60, 30, 1)) {
        pts.push_back({t_ms, p.pos});
        t_ms += 33;
    }
    for (int i = 0; i < 30; ++i) {
        pts.push_back({t_ms, 50});
        t_ms += 33;
    }

    const auto segs = samn::ClassifyMotion(pts, {});
    REQUIRE(segs.size() >= 3);
    CHECK(segs.front().state == samn::MotionState::Static);
    CHECK(segs.back().state == samn::MotionState::Static);
    bool saw_motion = false;
    for (const auto& s : segs) {
        if (s.state == samn::MotionState::Regular || s.state == samn::MotionState::Accelerating ||
            s.state == samn::MotionState::Decelerating)
            saw_motion = true;
    }
    CHECK(saw_motion);
}

// 0.7 TestClassifySegmentsAreContiguousAndLongEnough.
TEST_CASE("Classify: segments cover the curve and are long enough") {
    const auto pts = Sine(600, 30, 0.8);
    samn::ClassifyOptions opt;
    opt.min_segment_ms = 250;
    const auto segs = samn::ClassifyMotion(pts, opt);
    REQUIRE_FALSE(segs.empty());
    for (size_t i = 0; i < segs.size(); ++i) {
        CHECK(segs[i].end_ms >= segs[i].start_ms);
        if (segs.size() > 1) CHECK(segs[i].Duration() >= opt.min_segment_ms);
    }
    CHECK(segs.front().start_ms == pts.front().t_ms);
    CHECK(segs.back().end_ms == pts.back().t_ms);
}

// 0.7 applyDefaults does not set SmoothRadius to 2. A varying-but-regular
// speed has mean 108 raw and 108.6 after a radius of 2; an empty options
// value must stay on the raw side.
TEST_CASE("Classify: an empty options value does not smooth") {
    const std::vector<samn::MotionPoint> pts{{0, 0}, {1000, 100}, {2000, 220}, {3000, 320}, {4000, 440}};
    const auto bare = samn::ClassifyMotion(pts, {});
    samn::ClassifyOptions off;
    off.smooth_radius = 0;
    const auto raw = samn::ClassifyMotion(pts, off);
    samn::ClassifyOptions on;
    on.smooth_radius = 2;
    const auto smoothed = samn::ClassifyMotion(pts, on);
    REQUIRE(bare.size() == 1);
    REQUIRE(raw.size() == 1);
    REQUIRE(smoothed.size() == 1);
    CHECK(bare[0].mean_speed == doctest::Approx(108.0));
    CHECK(raw[0].mean_speed == doctest::Approx(108.0));
    CHECK(smoothed[0].mean_speed == doctest::Approx(108.6));
}
