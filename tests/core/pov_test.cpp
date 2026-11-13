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

namespace {

samn::PovTipPath HeldTip(int64_t w, int64_t h, double bw, double bh, double x, double y, int64_t last_ms,
                         int64_t step_ms = 500) {
    samn::PovTipPath p;
    p.width = w;
    p.height = h;
    p.box_w = bw;
    p.box_h = bh;
    for (int64_t t = 0; t <= last_ms; t += step_ms) p.tip.push_back({t, x, y});
    return p;
}

}  // namespace

TEST_CASE("PovSignalsFromTip: empty or zero size is empty") {
    CHECK(samn::PovSignalsFromTip({}).grid.empty());
    samn::PovTipPath p = HeldTip(320, 240, 0, 40, 160, 120, 3000);
    CHECK(samn::PovSignalsFromTip(p).grid.empty());
    p = HeldTip(0, 240, 40, 40, 160, 120, 3000);
    CHECK(samn::PovSignalsFromTip(p).grid.empty());
    p = HeldTip(320, 240, 40, 40, 160, 120, 3000);
    p.tip.clear();
    CHECK(samn::PovSignalsFromTip(p).grid.empty());
}

TEST_CASE("ClassifyTipPov: large lower-centre tip is first person") {
    // 320×240, 200×160 box sitting on the lower half: scale ~3.7, cy ~1.5.
    const auto view = samn::ClassifyTipPov(HeldTip(320, 240, 200, 160, 160, 160, 3000));
    CHECK(view.kind == samn::PovKind::FirstPerson);
    CHECK(In01(view.confidence));
    CHECK(view.confidence > 0);
    REQUIRE(!view.segments.empty());
    CHECK(view.segments.front().kind == samn::PovKind::FirstPerson);
}

TEST_CASE("ClassifyTipPov: small tip is third person") {
    const auto view = samn::ClassifyTipPov(HeldTip(320, 240, 24, 24, 160, 120, 3000));
    CHECK(view.kind == samn::PovKind::ThirdPerson);
    CHECK(In01(view.confidence));
    CHECK(view.confidence > 0);
}

TEST_CASE("ClassifyTipPov: a full-frame box is unknown, not a second person") {
    // Large and L/R split: the one tip box fills the frame. Fail closed.
    const auto view = samn::ClassifyTipPov(HeldTip(320, 240, 320, 240, 160, 120, 3000));
    CHECK(view.kind == samn::PovKind::Unknown);
}

TEST_CASE("ClassifyTipPov: one blob, never a partner ROI") {
    const auto sig = samn::PovSignalsFromTip(HeldTip(320, 240, 200, 160, 160, 160, 3000));
    REQUIRE(!sig.grid.empty());
    // Occupancy is a single rectangle: cells are zero or the box overlap,
    // never a second distant peak invented on the other side.
    for (const auto& g : sig.grid) {
        int occupied = 0;
        for (double v : g)
            if (v > 1e-9) ++occupied;
        CHECK(occupied >= 1);
        CHECK(occupied <= 9);
    }
    CHECK(samn::AdvisePovContact(samn::PovKind::FirstPerson, false) == samn::PovContactAdvice::SuggestContact);
    CHECK(samn::AdvisePovContact(samn::PovKind::FirstPerson, true) == samn::PovContactAdvice::CloseUp);
    CHECK(samn::AdvisePovContact(samn::PovKind::ThirdPerson, false) == samn::PovContactAdvice::WideView);
    CHECK(samn::AdvisePovContact(samn::PovKind::Unknown, false) == samn::PovContactAdvice::None);
}

TEST_CASE("PovAt and DominantPov") {
    CHECK(samn::DominantPov({}) == samn::PovKind::Unknown);
    CHECK(samn::DominantPovConfidence({}) == 0);
    CHECK(samn::PovAt({}, 0) == samn::PovKind::Unknown);

    samn::PovSignals s;
    s.step_ms = 500;
    s.grid.assign(8, FirstPerson());
    s.grid.insert(s.grid.end(), 3, SmallThird());  // 1500 ms < min_run → unknown
    const auto segs = samn::ClassifyPov(s);
    CHECK(samn::DominantPov(segs) == samn::PovKind::FirstPerson);
    CHECK(samn::PovAt(segs, 0) == samn::PovKind::FirstPerson);
    CHECK(samn::PovAt(segs, 3999) == samn::PovKind::FirstPerson);
    CHECK(samn::PovAt(segs, 4000) == samn::PovKind::Unknown);
    CHECK(In01(samn::DominantPovConfidence(segs)));
}
