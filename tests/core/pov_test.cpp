// Camera POV from synthetic 3×3 occupancy grids (core slice 1).
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "samn/pov.h"

namespace {

using Grid = std::array<double, 9>;

// Large centre + lower, low L/R split: a body filling the lower middle.
Grid FirstPerson() {
    return {0.05, 0.10, 0.05, 0.15, 0.80, 0.15, 0.40, 0.90, 0.40};
}

// A small blob in the middle of the frame.
Grid SmallThird() { return {0, 0, 0, 0, 0.2, 0, 0, 0, 0}; }

// Two side-by-side figures that do not fill the frame.
Grid BimodalThird() { return {0.05, 0.00, 0.05, 0.40, 0.05, 0.40, 0.05, 0.00, 0.05}; }

// A filled frame that is also L/R split: two people close in.
Grid LargeSplit() { return {0.80, 0.10, 0.80, 0.90, 0.20, 0.90, 0.80, 0.10, 0.80}; }

samn::PovSignals Repeat(const Grid& g, int n, int64_t step_ms = 500) {
    samn::PovSignals s;
    s.step_ms = step_ms;
    s.grid.assign(static_cast<size_t>(n), g);
    return s;
}

bool In01(double c) { return c >= 0.0 && c <= 1.0 && std::isfinite(c); }

}  // namespace

TEST_CASE("PovKindName") {
    CHECK(std::string(samn::PovKindName(samn::PovKind::Unknown)) == "unknown");
    CHECK(std::string(samn::PovKindName(samn::PovKind::FirstPerson)) == "first_person");
    CHECK(std::string(samn::PovKindName(samn::PovKind::ThirdPerson)) == "third_person");
}

TEST_CASE("ClassifyPov: empty grid is empty") {
    CHECK(samn::ClassifyPov({}).empty());
}

TEST_CASE("ClassifyPov: all-zero is unknown") {
    const auto segs = samn::ClassifyPov(Repeat(Grid{0, 0, 0, 0, 0, 0, 0, 0, 0}, 8));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::Unknown);
    CHECK(segs[0].start_ms == 0);
    CHECK(segs[0].end_ms == 4000);
    CHECK(segs[0].confidence == 0);
}

TEST_CASE("ClassifyPov: NaN and Inf are unknown") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    Grid g = FirstPerson();
    g[4] = nan;
    auto segs = samn::ClassifyPov(Repeat(g, 8));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::Unknown);
    CHECK(segs[0].confidence == 0);

    g = FirstPerson();
    g[8] = inf;
    segs = samn::ClassifyPov(Repeat(g, 8));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::Unknown);
}

TEST_CASE("ClassifyPov: large centre+lower, low split is first person") {
    const auto segs = samn::ClassifyPov(Repeat(FirstPerson(), 10));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::FirstPerson);
    CHECK(segs[0].start_ms == 0);
    CHECK(segs[0].end_ms == 5000);
    CHECK(In01(segs[0].confidence));
    CHECK(segs[0].confidence > 0);
}

TEST_CASE("ClassifyPov: small or L/R bimodal is third person") {
    auto segs = samn::ClassifyPov(Repeat(SmallThird(), 10));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::ThirdPerson);
    CHECK(In01(segs[0].confidence));
    CHECK(segs[0].confidence > 0);

    segs = samn::ClassifyPov(Repeat(BimodalThird(), 10));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::ThirdPerson);
    CHECK(In01(segs[0].confidence));
    CHECK(segs[0].confidence > 0);
}

TEST_CASE("ClassifyPov: large and split is unknown") {
    const auto segs = samn::ClassifyPov(Repeat(LargeSplit(), 10));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::Unknown);
    CHECK(In01(segs[0].confidence));
}

TEST_CASE("ClassifyPov: mid-clip switch") {
    samn::PovSignals s;
    s.step_ms = 500;
    s.grid.assign(8, FirstPerson());
    s.grid.insert(s.grid.end(), 8, SmallThird());
    const auto segs = samn::ClassifyPov(s);
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].kind == samn::PovKind::FirstPerson);
    CHECK(segs[0].start_ms == 0);
    CHECK(segs[0].end_ms == 4000);
    CHECK(segs[1].kind == samn::PovKind::ThirdPerson);
    CHECK(segs[1].start_ms == 4000);
    CHECK(segs[1].end_ms == 8000);
    CHECK(In01(segs[0].confidence));
    CHECK(In01(segs[1].confidence));
}

TEST_CASE("ClassifyPov: flicker shorter than min_run_ms is unknown") {
    samn::PovSignals s;
    s.step_ms = 500;
    for (int i = 0; i < 8; ++i) s.grid.push_back(i % 2 == 0 ? FirstPerson() : SmallThird());
    auto segs = samn::ClassifyPov(s);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].kind == samn::PovKind::Unknown);
    CHECK(segs[0].start_ms == 0);
    CHECK(segs[0].end_ms == 4000);

    // One 500 ms spike between two long first-person runs.
    s.grid.assign(8, FirstPerson());
    s.grid.push_back(SmallThird());
    s.grid.insert(s.grid.end(), 8, FirstPerson());
    segs = samn::ClassifyPov(s);
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].kind == samn::PovKind::FirstPerson);
    CHECK(segs[1].kind == samn::PovKind::Unknown);
    CHECK(segs[1].end_ms - segs[1].start_ms == 500);
    CHECK(segs[2].kind == samn::PovKind::FirstPerson);
}

TEST_CASE("ClassifyPov: confidence is in [0, 1]") {
    const Grid grids[] = {FirstPerson(), SmallThird(), BimodalThird(), LargeSplit(), Grid{}};
    for (const auto& g : grids) {
        for (const auto& seg : samn::ClassifyPov(Repeat(g, 10))) CHECK(In01(seg.confidence));
    }
}

TEST_CASE("ClassifyPov: options are unchanged") {
    samn::PovOptions opt;
    opt.min_run_ms = 3000;
    opt.scale_gate = 1.5;
    opt.split_gate = 0.4;
    opt.cy_gate = 1.0;
    const samn::PovOptions before = opt;
    (void)samn::ClassifyPov(Repeat(FirstPerson(), 10), opt);
    CHECK(opt.min_run_ms == before.min_run_ms);
    CHECK(opt.scale_gate == before.scale_gate);
    CHECK(opt.split_gate == before.split_gate);
    CHECK(opt.cy_gate == before.cy_gate);
}
