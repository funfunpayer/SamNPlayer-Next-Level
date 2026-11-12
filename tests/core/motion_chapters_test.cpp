#include <doctest/doctest.h>

#include "samn/motion_chapters.h"

using samn::MotionSegment;
using samn::MotionState;

// 0.7 motionx/chapters_test.go TestChaptersMapsStates.
TEST_CASE("Chapters map states onto pause, build, steady, crescendo, wind-down") {
    const std::vector<MotionSegment> segs{
        {MotionState::Static, 0, 1000, 0, 1},       {MotionState::Starting, 1000, 2000, 0, 1},
        {MotionState::Regular, 2000, 8000, 0, 1},   {MotionState::Accelerating, 8000, 9000, 0, 1},
        {MotionState::Stopping, 9000, 9500, 0, 1},
    };
    const auto ch = samn::MotionChaptersFromSegments(segs);
    // Five segments, no two neighbours share a kind. Index 3 of 5 is the
    // last quarter (5*3/4 == 3), so the acceleration there is a crescendo.
    REQUIRE(ch.size() == 5);
    CHECK(ch[0].kind == "pause");
    CHECK(ch[1].kind == "build");
    CHECK(ch[2].kind == "steady");
    CHECK(ch[3].kind == "crescendo");
    CHECK(ch[4].kind == "winddown");
    CHECK(ch[0].start_ms == 0);
    CHECK(ch[4].end_ms == 9500);
}

// 0.7 TestChaptersNil.
TEST_CASE("Chapters of an empty segment list are empty") {
    CHECK(samn::MotionChaptersFromSegments({}).empty());
}

TEST_CASE("Chapters merge adjacent stretches of the same kind") {
    const std::vector<MotionSegment> segs{
        {MotionState::Static, 0, 1000, 0, 1},
        {MotionState::Static, 1000, 2000, 0, 1},
        {MotionState::Regular, 2000, 3000, 10, 1},
    };
    const auto ch = samn::MotionChaptersFromSegments(segs);
    REQUIRE(ch.size() == 2);
    CHECK(ch[0].kind == "pause");
    CHECK(ch[0].start_ms == 0);
    CHECK(ch[0].end_ms == 2000);
    CHECK(ch[1].kind == "steady");
}

// len*3/4 is 0 for one segment, so a lone start is already a crescendo.
TEST_CASE("Chapters: a lone start is a crescendo") {
    const auto ch = samn::MotionChaptersFromSegments({{MotionState::Starting, 0, 1000, 1, 1}});
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].kind == "crescendo");
}
