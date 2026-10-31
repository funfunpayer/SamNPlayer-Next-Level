#include "samn/contact_preview.h"

#include "samn/mapper.h"

#include <doctest/doctest.h>

TEST_CASE("preview: span earlier is more active than deep only") {
    const samn::ContactVibrationPreview early = samn::PreviewContactVibration(0.40, "linear");
    const samn::ContactVibrationPreview deep = samn::PreviewContactVibration(0.90, "linear");
    REQUIRE(early.stroke.size() >= 20);
    REQUIRE(early.vib.size() == early.stroke.size());
    CHECK(early.active_pct > deep.active_pct + 5.0);
    CHECK(early.peak > 0);
    CHECK(deep.peak > 0);
}

TEST_CASE("preview: impulse is quieter than soft on the same span") {
    const samn::ContactVibrationPreview soft = samn::PreviewContactVibration(0.75, "soft");
    const samn::ContactVibrationPreview impulse = samn::PreviewContactVibration(0.75, "impulse");
    REQUIRE(soft.vib.size() == impulse.vib.size());
    CHECK(impulse.active_pct < soft.active_pct);
    CHECK(impulse.peak <= soft.peak + 1e-6);
    // Impulse only fires near the tip of the depth window → fewer peak dots.
    CHECK(impulse.peaks.size() <= soft.peaks.size());
}

TEST_CASE("preview: synthetic bounce covers a deep range") {
    const auto bounce = samn::SyntheticContactBounce();
    REQUIRE(bounce.size() >= 2);
    int lo = bounce.front().pos, hi = lo;
    for (const auto& a : bounce) {
        lo = std::min(lo, a.pos);
        hi = std::max(hi, a.pos);
    }
    CHECK(hi - lo >= 60);
    CHECK(bounce.back().at > bounce.front().at);
}

TEST_CASE("preview: strength scales the vib peak") {
    const samn::ContactVibrationPreview base = samn::PreviewContactVibration(0.75, "linear", 1.0);
    const samn::ContactVibrationPreview strong = samn::PreviewContactVibration(0.75, "linear", 1.5);
    CHECK(strong.peak == doctest::Approx(std::min(100.0, base.peak * 1.5)).epsilon(0.02));
}
