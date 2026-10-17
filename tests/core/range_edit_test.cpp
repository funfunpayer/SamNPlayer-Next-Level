#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

#include "samn/range_edit.h"

using samn::Action;
using samn::CapSpeedRange;
using samn::DeleteRange;
using samn::RangeEditError;
using samn::ScaleRangePos;
using samn::ScaleRangePosFade;
using samn::SnapMs;

// Go: TestSnapMs
TEST_CASE("TestSnapMs") {
    CHECK(SnapMs(100, 0) == 100);  // fps 0 passes through
    CHECK(SnapMs(100, -5) == 100);
    CHECK(SnapMs(100, std::numeric_limits<double>::quiet_NaN()) == 100);
    CHECK(SnapMs(100, std::numeric_limits<double>::infinity()) == 100);
    CHECK(SnapMs(40, 25) == 40);  // exactly one frame
    const auto half = SnapMs(20, 25);  // half a frame: Go accepts 0 or 40 (rounds away from 0: 40)
    CHECK((half == 0 || half == 40));
    CHECK(SnapMs(30, 25) == 40);
    CHECK(SnapMs(-30, 25) == -40);
}

// Go: TestDeleteRange
TEST_CASE("TestDeleteRange") {
    std::vector<Action> actions{{0, 0}, {100, 50}, {200, 100}, {300, 50}};
    auto out = DeleteRange(actions, 100, 200);
    REQUIRE(out.size() == 2);
    CHECK(out[0].at == 0);
    CHECK(out[1].at == 300);
    // swapped range, and the input is not modified
    CHECK(DeleteRange(actions, 200, 100).size() == 2);
    CHECK(actions.size() == 4);
    // fewer than 2 points left: error with the Go message
    try {
        DeleteRange(actions, 0, 300);
        FAIL("should throw");
    } catch (const RangeEditError& e) {
        CHECK(std::string(e.what()) == "funscript: deleting would leave fewer than 2 points");
    }
}

// Go: TestCapSpeedRange
TEST_CASE("TestCapSpeedRange") {
    std::vector<Action> actions{{0, 0}, {50, 100}, {1000, 100}};
    auto out = CapSpeedRange(actions, 0, 50, 400);
    // Need dt >= 500*100/400 = 125 ms for the first segment.
    CHECK(out[1].at - out[0].at >= 125);
    CHECK(out[2].at == 1075);  // later actions move right by the same amount
    CHECK(actions[1].at == 50);

    CHECK_THROWS_WITH_AS(CapSpeedRange({{0, 0}}, 0, 10, 400), "funscript: too few points", RangeEditError);
    // max_intensity <= 0 means 400
    CHECK(CapSpeedRange(actions, 0, 50, 0)[1].at == out[1].at);
    // segments outside the range stay untouched
    auto outside = CapSpeedRange(actions, 500, 900, 400);
    CHECK(outside[1].at == 50);
}

// Go: TestScaleRangePosFadeSoftEdges
TEST_CASE("TestScaleRangePosFadeSoftEdges") {
    std::vector<Action> actions{{0, 90}, {50, 90}, {100, 90}, {200, 10}};
    auto hard = ScaleRangePos(actions, 0, 100, 0.5);
    auto soft = ScaleRangePosFade(actions, 0, 100, 0.5, true);
    // Midpoint (50) matches the hard scale; edges keep the original (factor ramps to 1).
    CHECK(soft[1].pos == hard[1].pos);
    CHECK(soft[0].pos == 90);
    CHECK(soft[0].pos > hard[0].pos);
    // outside the range nothing changes; result is clamped to 0..100
    CHECK(hard[3].pos == 10);
    CHECK(ScaleRangePos(actions, 0, 100, 5.0)[0].pos == 100);
    CHECK(ScaleRangePos(actions, 0, 100, -5.0)[0].pos == 0);
    CHECK(ScaleRangePos(actions, 100, 0, 0.5)[0].pos == hard[0].pos);  // swapped range
}

// Go: TestCapSpeedRangeCapsAllInRangeSegments
TEST_CASE("TestCapSpeedRangeCapsAllInRangeSegments") {
    // Three 10 ms / 100 pos jumps = intensity 5000 each. Cap the whole [0,30].
    std::vector<Action> actions{{0, 0}, {10, 100}, {20, 0}, {30, 100}};
    auto out = CapSpeedRange(actions, 0, 30, 400);
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        const double dt = static_cast<double>(out[i + 1].at - out[i].at);
        const double dpos = std::fabs(static_cast<double>(out[i + 1].pos - out[i].pos));
        const double inten = 500.0 * dpos / dt;
        CHECK_MESSAGE(inten <= 400.5, "segment " << i << " intensity " << inten);
    }
}
