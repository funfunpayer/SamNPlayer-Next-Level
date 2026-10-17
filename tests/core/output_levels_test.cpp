#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <map>

#include "samn/output_levels.h"

using samn::ExtendedOOptions;
using samn::ExtendedOScale;
using samn::ExtendedOTotalMs;
using samn::OutputLevel;
using samn::ReducePeak;
using samn::SoftStartFactor;
using samn::SoftStartRunning;

// 0.7 player/speed_test.go TestReducePeak, player/extendedo_test.go and the
// ramps of player/sync.go softStartWithPositions and extendedo.go rampScale.

TEST_CASE("ReducePeak: knee 0.6, ratio 0.4, monotonic (0.7 TestReducePeak)") {
    const std::map<double, double> cases{{0, 0}, {0.3, 0.3}, {samn::kPeakKnee, samn::kPeakKnee}, {0.8, 0.68}, {1, 0.76}};
    for (const auto& [in, want] : cases) CHECK(ReducePeak(in) == doctest::Approx(want).epsilon(1e-9));
    double prev = -1;
    for (int i = 0; i <= 100; ++i) {
        const double got = ReducePeak(i / 100.0);
        CHECK(got >= prev);
        prev = got;
    }
    CHECK(ReducePeak(std::numeric_limits<double>::quiet_NaN()) == 0);
}

TEST_CASE("OutputLevel: peak first, then the channel level (0.7 TestSetOutputReducePeaks)") {
    CHECK(OutputLevel(1, true, 1, 0.5) == doctest::Approx(0.38).epsilon(1e-9));
    CHECK(OutputLevel(0.4, true, 1, 1) == doctest::Approx(0.4).epsilon(1e-9));  // below the knee
    CHECK(OutputLevel(1, false, 1, 0.5) == doctest::Approx(0.5).epsilon(1e-9));
}

TEST_CASE("OutputLevel applies the Extended-O scale (0.7 TestSetOutputAppliesIntensityScale)") {
    CHECK(OutputLevel(0.8, false, 0.25, 1) == doctest::Approx(0.2).epsilon(1e-9));
    CHECK(OutputLevel(0.4, false, 0.25, 1) == doctest::Approx(0.1).epsilon(1e-9));
}

TEST_CASE("OutputLevel: scale 0 is silent, never full power (0.7 TestExtendedOMinLevelZeroSilences, PS-11)") {
    CHECK(OutputLevel(0.8, false, 0, 1) == 0);
    CHECK(OutputLevel(0.8, true, 0, 1) == 0);
    CHECK(OutputLevel(1, false, 1, 0) == 0);
    // Out of range and non-finite inputs never raise the output.
    CHECK(OutputLevel(2, false, 3, 7) == 1);
    CHECK(OutputLevel(std::nan(""), false, 1, 1) == 0);
    CHECK(OutputLevel(1, false, std::nan(""), 1) == 0);
}

TEST_CASE("Soft start: 10 steps from 0.1 to 1 over the duration, 0 is off") {
    CHECK(SoftStartFactor(0, 500) == doctest::Approx(0.1));   // the first step at once
    CHECK(SoftStartFactor(49, 500) == doctest::Approx(0.1));
    CHECK(SoftStartFactor(50, 500) == doctest::Approx(0.2));
    CHECK(SoftStartFactor(250, 500) == doctest::Approx(0.6));
    CHECK(SoftStartFactor(450, 500) == doctest::Approx(1.0));  // step 10 (0.7 writes it, then waits a step)
    CHECK(SoftStartFactor(500, 500) == 1);
    CHECK(SoftStartFactor(100000, 500) == 1);
    CHECK(SoftStartFactor(-5, 500) == doctest::Approx(0.1));
    CHECK(SoftStartFactor(0, 0) == 1);
    CHECK(SoftStartRunning(499, 500));
    CHECK_FALSE(SoftStartRunning(500, 500));
    CHECK_FALSE(SoftStartRunning(0, 0));
    double prev = 0;
    for (int t = 0; t <= 600; t += 7) {
        const double f = SoftStartFactor(t, 500);
        CHECK(f >= prev);
        prev = f;
    }
}

TEST_CASE("Extended-O: 1 -> min over restore, hold, back up (0.7 rampScale)") {
    const ExtendedOOptions o{0.1, 10000, 500};  // the 0.7 defaults
    CHECK(ExtendedOTotalMs(o) == 11000);
    CHECK(ExtendedOScale(o, -1) == 1);
    // Down: step i at (i-1) * 50 ms is 1 + i/10 * (0.1 - 1).
    for (int i = 1; i <= 10; ++i) CHECK(ExtendedOScale(o, (i - 1) * 50) == doctest::Approx(1 - 0.09 * i));
    CHECK(ExtendedOScale(o, 500) == doctest::Approx(0.1));
    CHECK(ExtendedOScale(o, 10499) == doctest::Approx(0.1));
    // Up: step i is 0.1 + i/10 * 0.9.
    for (int i = 1; i <= 10; ++i) CHECK(ExtendedOScale(o, 10500 + (i - 1) * 50) == doctest::Approx(0.1 + 0.09 * i));
    CHECK(ExtendedOScale(o, 11000) == 1);
}

TEST_CASE("Extended-O: restore 0 switches at once") {
    const ExtendedOOptions o{0.5, 200, 0};
    CHECK(ExtendedOTotalMs(o) == 200);
    CHECK(ExtendedOScale(o, 0) == 0.5);
    CHECK(ExtendedOScale(o, 199) == 0.5);
    CHECK(ExtendedOScale(o, 200) == 1);
}

TEST_CASE("Extended-O: min level 0 is off, not unset (PS-11)") {
    const ExtendedOOptions o{0, 1000, 100};
    CHECK(ExtendedOScale(o, 100) == 0);
    CHECK(ExtendedOScale(o, 600) == 0);
    CHECK(ExtendedOScale(o, 95) == 0);  // the last down step
    CHECK(OutputLevel(1, false, ExtendedOScale(o, 600), 1) == 0);
    // Out of range values are clamped; a non-finite one is the safe side (0).
    CHECK(ExtendedOScale({-3, 1000, 0}, 10) == 0);
    CHECK(ExtendedOScale({7, 1000, 0}, 10) == 1);
    CHECK(ExtendedOScale({std::nan(""), 1000, 0}, 10) == 0);
    CHECK(ExtendedOTotalMs({0.1, -5, -5}) == 0);
}

TEST_CASE("Extended-O defaults are the 0.7 ones") {
    const ExtendedOOptions o;
    CHECK(o.minLevel == 0.1);
    CHECK(o.holdMs == 10000);
    CHECK(o.restoreMs == 500);
}
