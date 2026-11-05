#include <doctest/doctest.h>

#include "samn/funscript.h"

TEST_CASE("funscript parse: actions, clock-string duration") {
    auto s = samn::Parse(R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":"10:31"}})");
    REQUIRE(s.actions.size() == 2);
    CHECK(s.actions[1].at == 1000);
    CHECK(s.actions[1].pos == 100);
    CHECK(s.duration_meta == 631000);  // not 10 ms (std::stod stops at ':')
}

TEST_CASE("funscript parse: invalid JSON throws") {
    CHECK_THROWS(samn::Parse("{oops"));
}

// Review C5: funscript.cpp had its own time helpers. A huge duration was cast
// to int64 out of range, and a clock string with trailing text or an empty part
// read as a time.
TEST_CASE("funscript parse: a duration that is no time counts as not set") {
    const auto parse = [](const char* duration) {
        return samn::Parse(std::string(R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"duration":)") +
                           duration + "}}");
    };
    CHECK(parse("1e300").duration_meta == 0);
    CHECK(parse("-1e300").duration_meta == 0);
    CHECK(parse("\"1:30abc\"").duration_meta == 0);
    CHECK(parse("\"1::30\"").duration_meta == 0);
    CHECK(parse("\"1:30\"").duration_meta == 90000);
    CHECK(parse("12.5").duration_meta == 12500);  // seconds with a fraction, as OFS writes them
    CHECK(parse("null").duration_meta == 0);
}
