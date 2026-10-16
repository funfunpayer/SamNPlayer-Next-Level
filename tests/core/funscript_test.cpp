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
