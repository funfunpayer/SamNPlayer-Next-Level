// Port of 0.7 funscript/ringdown_test.go.
#include <doctest/doctest.h>

#include "samn/ringdown.h"

using samn::Action;
using samn::RingDown;

TEST_CASE("ringdown: ends at zero and keeps the prefix") {
    const std::vector<Action> in{{0, 10}, {1000, 90}};
    const auto out = RingDown(in, 1000, 90, 2);
    REQUIRE(out.size() > in.size());
    CHECK(out[0].at == in[0].at);
    CHECK(out[0].pos == in[0].pos);
    CHECK(out[1].at == in[1].at);
    CHECK(out[1].pos == in[1].pos);
    const Action& last = out.back();
    CHECK(last.pos == 0);
    CHECK(last.at > 1000);
    // Integer damping from 0.7: odd steps / 3, even steps the remainder, 180 ms.
    REQUIRE(out.size() == 7);
    CHECK(out[2].at == 1180);
    CHECK(out[2].pos == 24);
    CHECK(out[3].at == 1360);
    CHECK(out[3].pos == 54);
    CHECK(out[4].at == 1540);
    CHECK(out[4].pos == 12);
    CHECK(out[5].at == 1720);
    CHECK(out[5].pos == 18);
    CHECK(out[6].at == 1900);
    CHECK(out[6].pos == 0);
}

TEST_CASE("ringdown: clamps cycles") {
    const auto a = RingDown({}, 0, 80, 0);
    const auto b = RingDown({}, 0, 80, 9);
    // 0 clamps to one cycle (two half-cycles + the final 0); 9 clamps to two.
    CHECK(a.size() == 3);
    CHECK(b.size() == 5);
    CHECK(a.back().pos == 0);
    CHECK(b.back().pos == 0);
    CHECK(a.back().at == 540);
    CHECK(b.back().at == 900);
}
