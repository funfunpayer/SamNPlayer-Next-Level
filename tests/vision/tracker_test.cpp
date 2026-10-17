#include <doctest/doctest.h>

#include "samn/vision/tracker.h"

using namespace samn::vision;

// GD-02: one factory, CSRT available on every build, no fallback.
TEST_CASE("tracker: CSRT is created by the one factory") {
    std::string error;
    auto t = CreateTracker(TrackerKind::kCsrt, &error);
    REQUIRE_MESSAGE(t, error);
    CHECK(std::string(t->name()) == "CSRT");
}

TEST_CASE("tracker: the self-check passes on this build") {
    const std::string why = VisionSelfCheck();
    CHECK_MESSAGE(why.empty(), why);
    CHECK(OpenCvVersion().rfind("OpenCV 4.", 0) == 0);
}
