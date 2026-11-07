// The intensity arc (D-165) and the phase rules on synthetic arcs whose
// intro and climaxes are known.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>
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

// A shape given as (motion, loudness) at t seconds, 500 ms per step, with
// +-15 % jitter drawn separately for the motion and the sound.
template <typename F>
samn::PhaseSignals Shape(double seconds, F level) {
    samn::PhaseSignals s;
    s.step_ms = 500;
    uint32_t state = 7;
    const auto jitter = [&] {
        state = state * 1103515245u + 12345u;
        return double((state >> 16) % 1000) / 1000.0 - 0.5;
    };
    for (int i = 0; i < int(seconds * 2); ++i) {
        const auto [m, l] = level(i / 2.0);
        s.luma.push_back(0.5);
        s.motion.push_back(m * (1 + 0.3 * jitter()));
        s.loudness.push_back(std::min(1.0, l * (1 + 0.3 * jitter())));
    }
    return s;
}
using Level = std::pair<double, double>;
// Climaxes of 12 s centred on `centres`, action between, a calm 10 s start.
samn::PhaseSignals Bursts(double seconds, std::vector<double> centres, double half = 6) {
    return Shape(seconds, [centres, half](double t) -> Level {
        if (t < 10) return {.02, .2};
        for (double c : centres)
            if (std::abs(t - c) < half) return {.4, .9};
        return {.2, .3};
    });
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
int Count(const std::vector<samn::LabelPhase>& phases, const char* name) {
    return int(std::count_if(phases.begin(), phases.end(), [&](const samn::LabelPhase& p) { return p.name == name; }));
}
// Dark pictures over [from_s, to_s): an empty stretch for the phase rules.
samn::PhaseSignals Black(samn::PhaseSignals s, double from_s, double to_s) {
    for (size_t i = size_t(from_s * 2); i < size_t(to_s * 2) && i < s.luma.size(); ++i) s.luma[i] = 0;
    return s;
}
// The motion as a slow random walk under even sound: no climax anywhere.
samn::PhaseSignals Drift(uint32_t seed) {
    samn::PhaseSignals s;
    s.step_ms = 500;
    uint32_t state = seed;
    const auto jitter = [&] {
        state = state * 1103515245u + 12345u;
        return double((state >> 16) % 1000) / 1000.0 - 0.5;
    };
    double m = 0.2;
    for (int i = 0; i < 1200; ++i) {
        m = std::clamp(m + 0.01 * jitter(), 0.05, 0.4);
        s.luma.push_back(0.5);
        s.motion.push_back(m * (1 + 0.3 * jitter()));
        s.loudness.push_back(0.3);
    }
    return s;
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

TEST_CASE("arc: confidence ranks by prominence and drops when sound and motion disagree") {
    // A ranking value, not a probability: prominence / 0.4, capped at 1, x 0.7
    // when sound and motion top out more than 5 s apart.
    const auto early = samn::ComputeArc(EarlyClimax());
    REQUIRE(early.peaks.size() == 1);
    CHECK(early.peaks[0].confidence == doctest::Approx(1.0));
    const auto talky = samn::ComputeArc(TalkyIntro());
    REQUIRE_FALSE(talky.peaks.empty());
    CHECK(talky.peaks[0].confidence == doctest::Approx(0.811).epsilon(0.01));
    // Motion tops out at 60-72 s, the sound at 70-82 s: one climax, 10 s apart inside it.
    const auto apart = samn::ComputeArc(Clip({{10, .02, .2}, {50, .2, .3}, {10, .4, .3}, {2, .4, .9}, {10, .2, .9}, {38, .2, .3}}));
    REQUIRE(apart.peaks.size() == 1);
    CHECK(apart.peaks[0].confidence == doctest::Approx(0.7));
    // Even music has no top of its own near the motion's: it disagrees too.
    const auto even_music = samn::ComputeArc(MotionPeak());
    REQUIRE_FALSE(even_music.peaks.empty());
    CHECK(even_music.peaks[0].confidence == doctest::Approx(0.519).epsilon(0.01));
}

TEST_CASE("arc: secondary climaxes must be high enough, far enough and few") {
    // A small swell beside the climax is no second climax.
    const auto weak = samn::ComputeArc(Clip({{10, .02, .2}, {30, .2, .3}, {12, .24, .36}, {40, .2, .3}, {12, .4, .9}, {16, .05, .1}}));
    REQUIRE(weak.peaks.size() == 1);
    CHECK(std::abs(weak.peaks[0].t_ms / 1000.0 - 98) <= 5);
    // Four equal 10 s climaxes `gap` seconds apart: the primary and two more,
    // each on a true climax.
    for (int gap : {25, 35, 45}) {
        CAPTURE(gap);
        const auto many = samn::ComputeArc(Clip({{10, .02, .2}, {gap, .2, .3}, {10, .4, .9}, {gap, .2, .3}, {10, .4, .9},
                                                 {gap, .2, .3}, {10, .4, .9}, {gap, .2, .3}, {10, .4, .9}, {30, .2, .3}}));
        CHECK(many.peaks.size() == 3);
        for (const auto& p : many.peaks) {
            bool on_one = false;
            for (int k = 0; k < 4; ++k) on_one |= std::abs(p.t_ms / 1000.0 - (15 + gap + k * (gap + 10))) <= 5;
            CHECK(on_one);
        }
        for (size_t i = 0; i < many.peaks.size(); ++i)
            for (size_t j = i + 1; j < many.peaks.size(); ++j)
                CHECK(std::llabs(many.peaks[i].t_ms - many.peaks[j].t_ms) >= 20000);
    }
}

TEST_CASE("phases: a climax that runs to the clip's end is found") {
    for (int length : {15, 20}) {
        CAPTURE(length);
        const auto phases = samn::SuggestPhases(Clip({{10, .02, .2}, {190 - length, .2, .3}, {length, .4, .9}}));
        CHECK(Near(ClimaxMids(phases), 200 - length / 2.0, 5));
    }
}

TEST_CASE("phases: a clip that opens at its top has an opening, not a climax") {
    // Decided (D-165): the arc cannot tell a film cut in its climax from a
    // loud cold open, and a climax at 0 s would stop the Training at once.
    const auto open = samn::SuggestPhases(Clip({{15, .4, .9}, {185, .2, .3}}));
    for (double m : ClimaxMids(open)) CHECK(m > 30);
    const auto concert = samn::SuggestPhases(Clip({{30, .35, .95}, {215, .2, .35}, {13, .4, .9}, {42, .2, .35}}));
    CHECK(Near(ClimaxMids(concert), 251.5, 5));
    for (double m : ClimaxMids(concert)) CHECK(m > 40);
}

TEST_CASE("phases: a long plateau is one climax around its middle") {
    const auto phases = samn::SuggestPhases(Clip({{10, .02, .2}, {50, .2, .3}, {100, .35, .8}, {80, .2, .3}}));
    CHECK(Count(phases, "climax") == 1);
    CHECK(Near(ClimaxMids(phases), 110, 5));
}

TEST_CASE("phases: short clips keep their climax") {
    const auto c45 = samn::SuggestPhases(Clip({{8, .02, .2}, {18, .2, .3}, {8, .4, .9}, {11, .2, .3}}));
    CHECK(Near(ClimaxMids(c45), 30, 5));
    CHECK(std::abs(IntroEnd(c45) - 8) <= 3);
    const auto c30 = samn::SuggestPhases(Clip({{5, .02, .2}, {12, .2, .3}, {6, .4, .9}, {7, .2, .3}}));
    CHECK(Near(ClimaxMids(c30), 20, 5));
}

TEST_CASE("phases: two bursts close together: the climax sits on a burst, not in the dip") {
    const auto s = Bursts(240, {100, 115}, 5);  // 95-105 s and 110-120 s
    const auto mids = ClimaxMids(samn::SuggestPhases(s));
    REQUIRE_FALSE(mids.empty());
    for (double m : mids) CHECK((std::abs(m - 100) <= 3 || std::abs(m - 115) <= 3));
    const auto arc = samn::ComputeArc(s);
    REQUIRE_FALSE(arc.peaks.empty());
    CHECK((PeakNear(arc, 100, 3) || PeakNear(arc, 115, 3)));
}

TEST_CASE("phases: a black cut inside a climax leaves one climax") {
    const auto clip = Clip({{10, .02, .2}, {82, .2, .3}, {18, .4, .9}, {90, .2, .3}});  // climax 92-110 s
    for (const auto& [from, to] : {std::pair{104.0, 106.5}, std::pair{98.0, 101.0}}) {
        CAPTURE(from);
        const auto phases = samn::SuggestPhases(Black(clip, from, to));
        CHECK(Count(phases, "climax") == 1);
        CHECK(Near(ClimaxMids(phases), 101, 5));
        for (const auto& p : phases)
            if (p.name == "climax") CHECK(p.end_ms - p.start_ms >= samn::PhaseRuleOptions{}.climax_min_ms);
    }
}

TEST_CASE("phases: climaxes never merge past climax_max_ms") {
    samn::PhaseRuleOptions o;
    o.climax_min_ms = 30000;
    const auto phases = samn::SuggestPhases(Bursts(260, {100, 122, 144}), o);
    CHECK(Count(phases, "climax") >= 1);
    for (const auto& p : phases)
        if (p.name == "climax") CHECK(p.end_ms - p.start_ms <= o.climax_max_ms);
}

TEST_CASE("phases: slow drift is no climax") {
    int climaxes = 0;
    for (uint32_t seed = 1; seed <= 50; ++seed) climaxes += Count(samn::SuggestPhases(Drift(seed)), "climax");
    CHECK(climaxes <= 2);
}
