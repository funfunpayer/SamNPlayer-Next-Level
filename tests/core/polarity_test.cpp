// Which way is up (port of 0.7 funscript/polarity_test.go).
#include <doctest/doctest.h>

#include "samn/polarity.h"

using samn::Action;

TEST_CASE("polarity: a low first half suggests inverting") {
    const auto h = samn::SuggestPolarity({{0, 10}, {100, 20}, {200, 15}, {300, 80}, {400, 90}});
    CHECK(h.suggest_invert);
    CHECK(h.first_half_mean == doctest::Approx(15));
    CHECK(h.reason.find("invert") != std::string::npos);
}

TEST_CASE("polarity: a high first half suggests nothing") {
    const auto h = samn::SuggestPolarity({{0, 80}, {100, 90}, {200, 85}, {300, 20}, {400, 10}});
    CHECK_FALSE(h.suggest_invert);
    CHECK(h.reason.find("consistent") != std::string::npos);
}

TEST_CASE("polarity: a middle first half says nothing clear") {
    const auto h = samn::SuggestPolarity({{0, 50}, {100, 50}, {200, 50}, {300, 50}});
    CHECK_FALSE(h.suggest_invert);
    CHECK(h.reason.find("no clear direction") != std::string::npos);
}

TEST_CASE("polarity: too few points end without inverting") {
    const auto h = samn::SuggestPolarity({{0, 0}, {1, 1}});
    CHECK_FALSE(h.suggest_invert);
    CHECK_FALSE(h.reason.empty());
    CHECK(samn::SuggestPolarity({{5, 0}, {5, 10}, {5, 20}, {5, 30}}).reason == "the script has no duration");
}
