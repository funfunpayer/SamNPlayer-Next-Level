// The intensity arc (D-165) and the phase rules on synthetic arcs whose
// intro and climaxes are known.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

#include "samn/intensity_arc.h"
#include "samn/phase_rules.h"

namespace {

struct Seg {
    int seconds;
    double motion, loud;
};

// 500 ms per step, a plain picture, +-15 % jitter from a fixed LCG: the same
// clip on every run and every platform.
samn::PhaseSignals Clip(const std::vector<Seg>& segs) {
    samn::PhaseSignals s;
    s.step_ms = 500;
    uint32_t state = 1;
    for (const auto& g : segs)
        for (int i = 0; i < g.seconds * 2; ++i) {
            state = state * 1103515245u + 12345u;
            const double j = double((state >> 16) % 1000) / 1000.0 - 0.5;
            s.luma.push_back(0.5);
            s.motion.push_back(std::max(0.0, g.motion * (1 + 0.3 * j)));
            s.loudness.push_back(std::clamp(g.loud * (1 + 0.3 * j), 0.0, 1.0));
        }
    return s;
}

// A: talk, action, a climax at 30-45 s (a third into the clip), long action after it.
samn::PhaseSignals EarlyClimax() { return Clip({{10, .02, .2}, {20, .2, .3}, {15, .4, .9}, {65, .2, .3}}); }
// B: a loud talky intro of 20 s, the climax at 80-95 s.
samn::PhaseSignals TalkyIntro() { return Clip({{20, .03, .8}, {60, .2, .4}, {15, .35, .6}, {10, .05, .2}}); }
// C: 15 s calm, then motion and sound rise for 85 s and drop at 100 s.
samn::PhaseSignals BuildUp() {
    samn::PhaseSignals s;
    s.step_ms = 500;
    for (int i = 0; i < 240; ++i) {
        const double t = i / 2.0;
        s.luma.push_back(0.5);
        s.motion.push_back(t < 15 ? 0.02 : t < 100 ? 0.1 + 0.25 * (t - 15) / 85 : 0.05);
        s.loudness.push_back(t < 15 ? 0.2 : t < 100 ? 0.2 + 0.7 * (t - 15) / 85 : 0.1);
    }
    return s;
}
// D: two equal climaxes at 40-52 s and 92-104 s.
samn::PhaseSignals TwoClimaxes() {
    return Clip({{10, .02, .2}, {30, .2, .3}, {12, .4, .9}, {40, .2, .3}, {12, .4, .9}, {16, .05, .1}});
}
// E: the music stays as loud; only the motion peaks, at 70-85 s.
samn::PhaseSignals MotionPeak() { return Clip({{10, .02, .6}, {60, .2, .6}, {15, .4, .6}, {15, .05, .6}}); }

// The midpoints of the suggested climax stretches, in seconds.
std::vector<double> ClimaxMids(const std::vector<samn::LabelPhase>& phases) {
    std::vector<double> out;
    for (const auto& p : phases)
        if (p.name == "climax") out.push_back((p.start_ms + p.end_ms) / 2000.0);
    return out;
}
bool Near(const std::vector<double>& mids, double truth_s, double tolerance_s) {
    for (double m : mids)
        if (std::abs(m - truth_s) <= tolerance_s) return true;
    return false;
}
double IntroEnd(const std::vector<samn::LabelPhase>& phases) {
    for (const auto& p : phases)
        if (p.name == "intro") return p.end_ms / 1000.0;
    return -1;
}
bool PeakNear(const samn::IntensityArc& arc, double truth_s, double tolerance_s) {
    for (const auto& p : arc.peaks)
        if (std::abs(p.t_ms / 1000.0 - truth_s) <= tolerance_s) return true;
    return false;
}

}  // namespace

TEST_CASE("phases: a climax a third into the clip is found (A)") {
    const auto phases = samn::SuggestPhases(EarlyClimax());
    CHECK(Near(ClimaxMids(phases), 37.5, 5));
    CHECK(std::abs(IntroEnd(phases) - 10) <= 3);
}

TEST_CASE("phases: a loud talky intro still ends where the action begins (B)") {
    const auto phases = samn::SuggestPhases(TalkyIntro());
    CHECK(Near(ClimaxMids(phases), 87.5, 2));
    CHECK(std::abs(IntroEnd(phases) - 20) <= 2);
}

TEST_CASE("phases: the climax of a build-up is its top (C)") {
    const auto phases = samn::SuggestPhases(BuildUp());
    CHECK(Near(ClimaxMids(phases), 95, 5));
    CHECK(std::abs(IntroEnd(phases) - 15) <= 3);
}

TEST_CASE("phases: two climaxes are both suggested (D)") {
    const auto phases = samn::SuggestPhases(TwoClimaxes());
    const auto mids = ClimaxMids(phases);
    CHECK(Near(mids, 98, 5));
    CHECK(Near(mids, 46, 5));
    CHECK(std::abs(IntroEnd(phases) - 10) <= 3);
}

TEST_CASE("phases: a motion peak under even music is the climax (E)") {
    const auto phases = samn::SuggestPhases(MotionPeak());
    CHECK(Near(ClimaxMids(phases), 77.5, 2));
    CHECK(std::abs(IntroEnd(phases) - 10) <= 3);
}

TEST_CASE("arc: the five arcs peak where they were made to") {
    const auto a = samn::ComputeArc(EarlyClimax());
    REQUIRE(a.peaks.size() == 1);
    CHECK(std::abs(a.peaks[0].t_ms / 1000.0 - 37.5) <= 5);
    CHECK(std::abs(a.intro_end_ms / 1000.0 - 10) <= 3);
    const auto b = samn::ComputeArc(TalkyIntro());
    REQUIRE_FALSE(b.peaks.empty());
    CHECK(std::abs(b.peaks[0].t_ms / 1000.0 - 87.5) <= 2);
    CHECK(std::abs(b.intro_end_ms / 1000.0 - 20) <= 3);
    const auto c = samn::ComputeArc(BuildUp());
    REQUIRE_FALSE(c.peaks.empty());
    CHECK(std::abs(c.peaks[0].t_ms / 1000.0 - 95) <= 5);
    CHECK(std::abs(c.intro_end_ms / 1000.0 - 15) <= 3);
    const auto d = samn::ComputeArc(TwoClimaxes());
    REQUIRE(d.peaks.size() == 2);
    CHECK(std::abs(d.peaks[0].t_ms / 1000.0 - 98) <= 5);  // equal heights: the later one leads
    CHECK(std::abs(d.peaks[1].t_ms / 1000.0 - 46) <= 5);
    CHECK(std::abs(d.intro_end_ms / 1000.0 - 10) <= 3);
    const auto e = samn::ComputeArc(MotionPeak());
    REQUIRE_FALSE(e.peaks.empty());
    CHECK(std::abs(e.peaks[0].t_ms / 1000.0 - 77.5) <= 2);
    CHECK(std::abs(e.intro_end_ms / 1000.0 - 10) <= 3);
    for (const auto* arc : {&a, &b, &c, &d, &e})
        for (const auto& p : arc->peaks) {
            CHECK(p.start_ms <= p.t_ms);
            CHECK(p.t_ms < p.end_ms);
            CHECK(p.confidence >= 0);
            CHECK(p.confidence <= 1);
        }
}

TEST_CASE("arc: an even clip has neither a climax nor an intro") {
    samn::PhaseSignals flat;
    flat.step_ms = 500;
    flat.motion.assign(240, 0.2);
    flat.loudness.assign(240, 0.3);
    const auto a = samn::ComputeArc(flat);
    CHECK(a.peaks.empty());
    CHECK(a.intro_end_ms == 0);
    REQUIRE(a.detail.size() == 240);
    REQUIRE(a.arc.size() == 240);
    const auto jittered = samn::ComputeArc(Clip({{120, .2, .3}}));
    CHECK(jittered.peaks.empty());
    CHECK(jittered.intro_end_ms == 0);
}

TEST_CASE("arc: a silent clip is the picture alone") {
    auto silent = EarlyClimax();
    std::fill(silent.loudness.begin(), silent.loudness.end(), 0.0);
    auto mute = EarlyClimax();
    mute.loudness.clear();
    const auto a = samn::ComputeArc(silent), b = samn::ComputeArc(mute);
    CHECK(a.detail == b.detail);
    REQUIRE(a.peaks.size() == b.peaks.size());
    REQUIRE_FALSE(a.peaks.empty());
    CHECK(a.peaks[0].t_ms == b.peaks[0].t_ms);
    CHECK(PeakNear(a, 37.5, 5));
}

TEST_CASE("arc: a clip without motion is the sound alone") {
    auto sound = TwoClimaxes();
    sound.motion.clear();
    const auto a = samn::ComputeArc(sound);
    CHECK(a.detail.size() == sound.loudness.size());
    CHECK(PeakNear(a, 98, 5));
    CHECK(PeakNear(a, 46, 5));
    auto picture = TwoClimaxes();
    picture.loudness.resize(10);  // shorter than the clip: missing
    const auto b = samn::ComputeArc(picture);
    CHECK(PeakNear(b, 98, 5));
    CHECK(PeakNear(b, 46, 5));
}

TEST_CASE("arc: short, empty and broken clips") {
    CHECK(samn::ComputeArc({}).detail.empty());
    samn::PhaseSignals s;
    s.step_ms = 0;
    s.motion.assign(100, 0.2);
    CHECK(samn::ComputeArc(s).detail.empty());
    for (const size_t n : std::vector<size_t>{1, 2, 3, 9}) {
        samn::PhaseSignals tiny;
        tiny.motion.assign(n, 0.3);
        tiny.loudness.assign(n, 0.5);
        tiny.motion[0] = 0.9;
        const auto a = samn::ComputeArc(tiny);
        CHECK(a.detail.size() == n);
        CHECK(a.intro_end_ms == 0);
    }
    samn::PhaseSignals dark;
    dark.motion.assign(50, 0.0);
    dark.loudness.assign(50, 0.0);
    const auto d = samn::ComputeArc(dark);
    CHECK(d.peaks.empty());
    CHECK(d.intro_end_ms == 0);
}

TEST_CASE("arc: NaN, infinite and negative values count as 0") {
    auto s = EarlyClimax();
    for (size_t i = 0; i < s.motion.size(); i += 17) s.motion[i] = std::nan("");
    for (size_t i = 5; i < s.loudness.size(); i += 23) s.loudness[i] = -1;
    s.motion[3] = -0.5;
    s.loudness[90] = std::numeric_limits<double>::infinity();
    s.motion[91] = std::numeric_limits<double>::infinity();
    const auto a = samn::ComputeArc(s);
    for (double v : a.detail) CHECK(std::isfinite(v));
    for (double v : a.arc) CHECK(std::isfinite(v));
    CHECK(PeakNear(a, 37.5, 5));
    samn::PhaseSignals broken;
    broken.motion.assign(100, std::nan(""));
    broken.loudness.assign(100, -2.0);
    CHECK(samn::ComputeArc(broken).peaks.empty());
}

TEST_CASE("arc: confidence follows the prominence, and sound and motion must agree") {
    // Motion tops out at 60-72 s, the sound at 70-82 s: one climax, 10 s apart inside it.
    const auto apart = samn::ComputeArc(Clip({{10, .02, .2}, {50, .2, .3}, {10, .4, .3}, {2, .4, .9}, {10, .2, .9}, {38, .2, .3}}));
    REQUIRE(apart.peaks.size() == 1);
    const auto& p = apart.peaks[0];
    CHECK(p.confidence == doctest::Approx(0.7 * std::min(1.0, p.prominence / 0.4)));
    const auto together = samn::ComputeArc(EarlyClimax());
    REQUIRE(together.peaks.size() == 1);
    CHECK(together.peaks[0].confidence == doctest::Approx(std::min(1.0, together.peaks[0].prominence / 0.4)));
    CHECK(together.peaks[0].confidence > 0.9);
}

TEST_CASE("arc: secondary climaxes must be high enough, far enough and few") {
    // A small swell beside the climax is no second climax.
    const auto weak = samn::ComputeArc(Clip({{10, .02, .2}, {30, .2, .3}, {12, .24, .36}, {40, .2, .3}, {12, .4, .9}, {16, .05, .1}}));
    REQUIRE(weak.peaks.size() == 1);
    CHECK(std::abs(weak.peaks[0].t_ms / 1000.0 - 98) <= 5);
    // Four equal climaxes: the primary and two more. (Bursts much closer
    // together lift the arc's median and fail the gain of 1.25 x median.)
    const auto many = samn::ComputeArc(Clip({{10, .02, .2}, {45, .2, .3}, {10, .4, .9}, {45, .2, .3}, {10, .4, .9},
                                             {45, .2, .3}, {10, .4, .9}, {45, .2, .3}, {10, .4, .9}, {30, .2, .3}}));
    CHECK(many.peaks.size() == 3);
    for (size_t i = 0; i < many.peaks.size(); ++i)
        for (size_t j = i + 1; j < many.peaks.size(); ++j)
            CHECK(std::llabs(many.peaks[i].t_ms - many.peaks[j].t_ms) >= 20000);
}
