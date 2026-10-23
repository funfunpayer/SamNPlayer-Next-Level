#include <doctest/doctest.h>

#include <cmath>

#include "samn/engine/tip_distance.h"

using namespace samn::engine;

TEST_CASE("tip_distance: small tip nearest point is near the centre") {
    // Tip 10×10; partner centre 95 px to the right at the same height.
    // Nearest tip point is the right edge (half-width from the tip mid).
    const cv::Rect tip(0, 0, 10, 10);
    const cv::Rect partner(95, 0, 10, 10);
    const double d = TipPartnerDistance(tip, partner);
    const double centre_to_centre = std::hypot(95.0, 0.0);
    CHECK(std::abs(d - centre_to_centre) <= tip.width / 2.0);
    CHECK(d == doctest::Approx(90.0));
}

TEST_CASE("tip_distance: large tip uses the near edge") {
    // Tip 200 px wide; partner centre at x=260 → edge gap 60, not mid gap 160.
    const cv::Rect tip(0, 0, 200, 40);
    const cv::Rect partner(250, 10, 20, 20);
    CHECK(TipPartnerDistance(tip, partner) == doctest::Approx(60.0));
    CHECK(TipPartnerDistance(tip, partner) < 100.0);
}

TEST_CASE("tip_distance: overlap is zero") {
    CHECK(TipPartnerDistance(cv::Rect(0, 0, 50, 50), cv::Rect(40, 40, 50, 50)) == 0);
    CHECK(TipPartnerDistance(cv::Rect(10, 10, 20, 20), cv::Rect(10, 10, 20, 20)) == 0);
}

TEST_CASE("tip_distance: empty boxes are zero") {
    CHECK(TipPartnerDistance(cv::Rect(0, 0, 0, 10), cv::Rect(20, 0, 10, 10)) == 0);
    CHECK(TipPartnerDistance(cv::Rect(0, 0, 10, 10), cv::Rect(20, 0, 0, 10)) == 0);
}

TEST_CASE("tip_distance: pure horizontal and pure vertical gaps") {
    // Tip right/bottom edge to partner centre (not box-to-box gap).
    CHECK(TipPartnerDistance(cv::Rect(0, 0, 20, 20), cv::Rect(80, 0, 20, 20)) == doctest::Approx(70.0));
    CHECK(TipPartnerDistance(cv::Rect(0, 0, 20, 20), cv::Rect(0, 80, 20, 20)) == doctest::Approx(70.0));
}
