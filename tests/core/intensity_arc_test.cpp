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
// Motion and sound as random walks of `step` per 500 ms (no climax
// anywhere), +-15 % jitter, an optional calm intro, the loudness divided by
// its loudest step as the label editor does.
samn::PhaseSignals Walk(uint32_t seed, double step, bool intro) {
    samn::PhaseSignals s;
    s.step_ms = 500;
    uint32_t state = seed;
    const auto u = [&] {
        state = state * 1103515245u + 12345u;
        return double((state >> 16) % 1000) / 1000.0;
    };
    double m = 0.2, l = 0.3;
    for (int i = 0; i < 1200; ++i) {
        m = std::clamp(m + step * (u() - 0.5), 0.05, 0.4);
        l = std::clamp(l + step * (u() - 0.5), 0.1, 0.6);
        const bool calm = intro && i < 30;
        s.luma.push_back(0.5);
        s.motion.push_back((calm ? 0.02 : m) * (1 + 0.3 * (u() - 0.5)));
        s.loudness.push_back((calm ? 0.15 : l) * (1 + 0.3 * (u() - 0.5)));
    }
    const double top = *std::max_element(s.loudness.begin(), s.loudness.end());
    for (auto& v : s.loudness) v /= top;
    return s;
}
// A clip of `seconds` at 500 ms per step: steady action (motion 0.2 +-30 %,
// loudness 0.3 +-20 %) wherever `blank(t)` is false; there the picture is
// black (luma 0) or still (luma 0.5, motion 0) and silent.
template <typename F>
samn::PhaseSignals WithBlanks(double seconds, uint32_t seed, bool black, F blank) {
    samn::PhaseSignals s;
    s.step_ms = 500;
    uint32_t state = seed;
    const auto u = [&] {
        state = state * 1103515245u + 12345u;
        return double((state >> 16) % 1000) / 1000.0;
    };
    for (int i = 0; i < int(seconds * 2); ++i) {
        const bool b = blank(i / 2.0);
        const double m = 0.2 * (1 + 0.6 * (u() - 0.5)), l = 0.3 * (1 + 0.4 * (u() - 0.5));
        s.luma.push_back(b && black ? 0.0 : 0.5);
        s.motion.push_back(b ? 0.0 : m);
        s.loudness.push_back(b ? 0.0 : l);
    }
    return s;
}
// The loudness divided by its loudest step, as the label editor does.
samn::PhaseSignals Normalised(samn::PhaseSignals s) {
    const double top = *std::max_element(s.loudness.begin(), s.loudness.end());
    if (top > 0)
        for (auto& v : s.loudness) v /= top;
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
    // A step so long that the clip's times would not fit in 64 bits: no arc,
    // no phases (the app uses 500 ms).
    s.luma.assign(100, 0.5);
    for (const int64_t huge : {std::numeric_limits<int64_t>::max() / 100, std::numeric_limits<int64_t>::max()}) {
        s.step_ms = huge;
        CHECK(samn::ComputeArc(s).detail.empty());
        CHECK(samn::SuggestPhases(s).empty());
    }
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

TEST_CASE("arc: confidence is a bounded ranking value") {
    // In 0..1, larger for a stronger climax, at most 0.7 when sound and
    // motion top out more than 5 s apart.
    for (const auto& clip : {EarlyClimax(), TalkyIntro(), BuildUp(), TwoClimaxes(), MotionPeak()})
        for (const auto& p : samn::ComputeArc(clip).peaks) {
            CHECK(p.confidence >= 0);
            CHECK(p.confidence <= 1);
        }
    const auto weak = samn::ComputeArc(Clip({{10, .02, .2}, {60, .2, .3}, {12, .3, .45}, {40, .2, .3}}));
    const auto strong = samn::ComputeArc(Clip({{10, .02, .2}, {60, .2, .3}, {12, .34, .55}, {40, .2, .3}}));
    REQUIRE(weak.peaks.size() == 1);
    REQUIRE(strong.peaks.size() == 1);
    CHECK(strong.peaks[0].confidence > weak.peaks[0].confidence);
    // Motion tops out at 70-84 s, the sound at 80-94 s: one climax at full
    // strength, its inputs 10 s apart, 0.7 x the confidence of the same climax
    // with sound and motion topping out together.
    const auto apart = samn::ComputeArc(Clip({{10, .02, .1}, {60, .12, .15}, {10, .6, .15}, {4, .6, .95}, {10, .12, .95}, {40, .12, .15}}));
    const auto together = samn::ComputeArc(Clip({{10, .02, .1}, {60, .12, .15}, {14, .6, .95}, {10, .12, .15}, {40, .12, .15}}));
    REQUIRE(apart.peaks.size() == 1);
    REQUIRE(together.peaks.size() == 1);
    CHECK(together.peaks[0].confidence == doctest::Approx(1.0));
    CHECK(apart.peaks[0].confidence == doctest::Approx(0.7 * together.peaks[0].confidence));
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
    for (const auto& cut : {std::pair{104.0, 106.5}, std::pair{98.0, 101.0}})
        for (bool noisy : {false, true}) {
            const double from = cut.first, to = cut.second;  // plain locals: CAPTURE cannot take a structured binding
            CAPTURE(from);
            CAPTURE(noisy);
            auto s = Black(clip, from, to);
            if (noisy)  // the cut as a codec gives it: a little motion, the sound going on
                for (size_t i = size_t(from * 2); i < size_t(to * 2); ++i) {
                    s.luma[i] = 0.02;
                    s.motion[i] = 0.002;
                }
            const auto phases = samn::SuggestPhases(s);
            CHECK(Count(phases, "climax") == 1);
            if (!noisy) {
                CHECK(Near(ClimaxMids(phases), 101, 5));
            } else {
                // Without its motion the cut splits the climax in two bursts
                // and the arc may pick either: the climax lies inside the true one.
                for (double m : ClimaxMids(phases)) CHECK((m >= 92 && m <= 110));
            }
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

TEST_CASE("phases: random walks get few climaxes") {
    // 40 clips of 600 s per step size, half of them with a calm intro. The
    // bounds are absolute, per clip: 0.05, 0.15 and 0.55, with margin over
    // what was measured when they were set (0, 0.075 and 0.45; the rules
    // before D-165 gave 0, 0.05 and 0.525 on these clips). Step 0.02 is the
    // regime where both rule sets are unreliable
    // (docs/benchmarks/INTENSITY_ARC_2026-11-06.md).
    const std::pair<double, double> bounds[] = {{0.005, 0.05}, {0.01, 0.15}, {0.02, 0.55}};
    for (const auto& bound : bounds) {
        const double step = bound.first, per_clip = bound.second;  // plain locals for CAPTURE
        CAPTURE(step);
        int climaxes = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed) climaxes += Count(samn::SuggestPhases(Walk(seed, step, seed % 2 == 0)), "climax");
        CHECK(climaxes / 40.0 <= per_clip);
    }
}

TEST_CASE("phases: a calm intro before even action is no climax") {
    // The intro is no part of the action: the even action after it is no
    // climax, whatever its length.
    for (double intro : {5.0, 10.0, 20.0, 30.0})
        for (double length : {60.0, 200.0, 600.0}) {
            CAPTURE(intro);
            CAPTURE(length);
            const auto s = Shape(length, [intro](double t) -> Level { return t < intro ? Level{.02, .2} : Level{.2, .3}; });
            CHECK(Count(samn::SuggestPhases(s), "climax") == 0);
        }
    const auto tail = Shape(300, [](double t) -> Level { return t < 15 ? Level{.02, .2} : t > 270 ? Level{.02, .1} : Level{.2, .3}; });
    CHECK(Count(samn::SuggestPhases(tail), "climax") == 0);
    const auto beats = Shape(300, [](double t) -> Level {
        return t < 20 ? Level{.02, .1} : Level{.2, std::fmod(t, 1.0) < .5 ? .7 : .2};
    });
    CHECK(Count(samn::SuggestPhases(beats), "climax") == 0);
    const auto all_top = Shape(200, [](double t) -> Level { return t < 20 ? Level{.02, .2} : Level{.4, .9}; });
    CHECK(Count(samn::SuggestPhases(all_top), "climax") == 0);
}

TEST_CASE("phases: a climax in the sound alone is found") {
    // A still camera, the music swells: motion even (or no picture motion at
    // all), loudness from 0.3 to 0.9 over 12 s.
    const auto even = Normalised(Shape(300, [](double t) -> Level {
        return t < 15 ? Level{.02, .2} : std::abs(t - 220) < 6 ? Level{.2, .9} : Level{.2, .3};
    }));
    CHECK(Near(ClimaxMids(samn::SuggestPhases(even)), 220, 5));
    auto still = Shape(300, [](double t) -> Level {
        return t < 10 ? Level{0, .1} : std::abs(t - 220) < 6 ? Level{0, .6} : Level{0, .35};
    });
    const auto arc = samn::ComputeArc(still);
    CHECK(PeakNear(arc, 220, 5));
    // A small rise of the motion with a moderate one of the sound.
    const auto mixed = Normalised(Shape(300, [](double t) -> Level {
        return t < 15 ? Level{.02, .2} : std::abs(t - 220) < 6 ? Level{.26, .6} : Level{.2, .3};
    }));
    CHECK(Near(ClimaxMids(samn::SuggestPhases(mixed)), 220, 5));
}

TEST_CASE("phases: a climax that fades from its own level has a cool-down") {
    // Where such a climax ends is ambiguous: the plateau at 144-156 s fades
    // from its own level, so the true top runs on into the fade until its
    // first calm step (the motion back at the action's 0.2, 236 s). The
    // climax must lie inside that top, its midpoint within 8 s of the
    // plateau's middle (150 s).
    const auto s = Shape(300, [](double t) -> Level {
        if (t < 10) return {.02, .2};
        if (t < 144) return {.2, .3};
        if (t < 156) return {.4, .9};
        const double f = 1 - (t - 156) / 160;
        return {.4 * f, .9 * f};
    });
    const auto phases = samn::SuggestPhases(s);
    REQUIRE(Count(phases, "climax") == 1);
    for (const auto& p : phases)
        if (p.name == "climax") {
            CHECK(p.start_ms >= 144000);
            CHECK(p.end_ms <= 236000);
        }
    CHECK(Near(ClimaxMids(phases), 150, 8));
    CHECK(Count(phases, "cool-down") == 1);
}

TEST_CASE("phases: a staircase whose top step runs to the end has its climax there") {
    const auto s = Shape(300, [](double t) -> Level {
        if (t < 10) return {.02, .2};
        const int k = int((t - 10) / 58);
        return {.1 + .06 * k, .2 + .15 * k};
    });
    const auto mids = ClimaxMids(samn::SuggestPhases(s));
    REQUIRE(mids.size() == 1);
    CHECK(mids[0] >= 242);
}

TEST_CASE("phases: a climax boxed in by black cuts takes its longest free piece") {
    const auto clip = Clip({{10, .02, .2}, {82, .2, .3}, {18, .4, .9}, {90, .2, .3}});  // climax 92-110 s
    const auto phases = samn::SuggestPhases(Black(Black(clip, 98, 101), 105, 107));
    REQUIRE(Count(phases, "climax") == 1);
    for (const auto& p : phases)
        if (p.name == "climax") {
            CHECK(p.end_ms <= 98000);  // the 6 s piece before the first cut, not the 4 s one between them
            CHECK(p.end_ms - p.start_ms >= 6000);
        }
}

TEST_CASE("phases: long empty stretches are no climax") {
    // Steady action with black or still and silent stretches: nothing rises
    // within the action, whatever the share of empty pictures.
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        CAPTURE(seed);
        for (double share : {0.5, 0.6, 0.8}) {
            CAPTURE(share);
            const auto middle = [share](double t) { return std::abs(t - 300) < share * 300; };
            CHECK(Count(samn::SuggestPhases(WithBlanks(600, seed, true, middle)), "climax") == 0);
            CHECK(Count(samn::SuggestPhases(WithBlanks(600, seed, false, middle)), "climax") == 0);
        }
        CHECK(Count(samn::SuggestPhases(WithBlanks(600, seed, true, [](double t) { return t >= 240; })), "climax") == 0);
        CHECK(Count(samn::SuggestPhases(WithBlanks(600, seed, true, [](double t) { return t < 360; })), "climax") == 0);
    }
}

TEST_CASE("phases: a long black stretch with codec noise is no climax") {
    // Black frames are not exactly still: the codec's noise gives a little
    // motion (0.002 +-50 %), and the sound may be silent or go on (music at
    // 0.25 +-20 %). Steady action on either side, 50 % or 70 % black between.
    for (uint32_t seed = 1; seed <= 4; ++seed)
        for (double share : {0.5, 0.7})
            for (bool music : {false, true}) {
                CAPTURE(seed);
                CAPTURE(share);
                CAPTURE(music);
                const auto middle = [share](double t) { return std::abs(t - 300) < share * 300; };
                auto s = WithBlanks(600, seed, true, middle);
                uint32_t state = seed * 7919u;
                const auto u = [&] {
                    state = state * 1103515245u + 12345u;
                    return double((state >> 16) % 1000) / 1000.0;
                };
                for (size_t i = 0; i < s.luma.size(); ++i)
                    if (s.luma[i] == 0) {
                        s.luma[i] = 0.02;
                        s.motion[i] = 0.002 * (1 + (u() - 0.5));
                        s.loudness[i] = music ? 0.25 * (1 + 0.4 * (u() - 0.5)) : 0;
                    }
                CHECK(Count(samn::SuggestPhases(s), "climax") == 0);
            }
}

TEST_CASE("phases: a peak beside a long black stretch is still found") {
    auto s = WithBlanks(600, 3, true, [](double t) { return t >= 300; });
    for (size_t i = 0; i < 600; ++i) {
        const double t = i / 2.0;
        if (t < 15) {
            s.motion[i] *= 0.1;
            s.loudness[i] *= 0.33;
        } else if (std::abs(t - 150) < 6) {
            s.motion[i] *= 1.9;
            s.loudness[i] = std::min(0.95, s.loudness[i] * 1.9);
        }
    }
    const auto phases = samn::SuggestPhases(s);
    CHECK(Count(phases, "climax") == 1);
    CHECK(Near(ClimaxMids(phases), 150, 5));
}

TEST_CASE("phases: scene cuts in a clip without sound are no climax") {
    // No sound; a calm intro, then steady motion (knots +-10 %, about
    // Gaussian jitter of sd 0.15), and a cut (one frame of 3x motion) on 2 %
    // of the samples, about one every 25 s.
    int climaxes = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        uint32_t state = seed * 2654435761u;
        const auto u = [&] {
            state = state * 1103515245u + 12345u;
            return double((state >> 16) % 1000) / 1000.0;
        };
        samn::PhaseSignals s;
        s.step_ms = 500;
        const double base = 0.1 + 0.2 * u(), intro = 15 + 25 * u();
        const double k1 = 1 + 0.2 * (u() - 0.5), k2 = 1 + 0.2 * (u() - 0.5);
        for (int i = 0; i < 1200; ++i) {
            const double t = i / 2.0, knot = k1 + (k2 - k1) * t / 600;
            double m = base * knot * (t < intro ? 0.2 : 1.0) * (1 + 0.3 * (u() + u() + u() - 1.5));
            if (u() < 0.02) m *= 3;
            s.luma.push_back(0.5);
            s.motion.push_back(std::max(0.0, m));
        }
        climaxes += Count(samn::SuggestPhases(s), "climax");
    }
    CHECK(climaxes == 0);
    // A cut (20x) on the clip's first or last two samples is no climax either,
    // also where the action slowly rises 20 % towards it; the calm 30 s are at
    // the other end.
    for (uint32_t seed : {3u, 4u})
        for (int at : {0, 1, 1198, 1199}) {
            CAPTURE(seed);
            CAPTURE(at);
            samn::PhaseSignals s;
            s.step_ms = 500;
            uint32_t state = seed;
            for (int i = 0; i < 1200; ++i) {
                state = state * 1103515245u + 12345u;
                const double j = double((state >> 16) % 1000) / 1000.0 - 0.5;
                const bool late = at > 600;
                const bool calm = late ? i < 60 : i >= 1140;
                const double level = 0.2 * (1 + 0.2 * (late ? i : 1199 - i) / 1199.0);
                s.luma.push_back(0.5);
                s.motion.push_back((calm ? 0.04 : level) * (1 + 0.3 * j));
            }
            s.motion[size_t(at)] *= 20;
            CHECK(Count(samn::SuggestPhases(s), "climax") == 0);
        }
}

TEST_CASE("phases: a loud opening is no climax, also under heavy jitter") {
    // A loud, busy opening (2x motion, 2.2x loudness) for 20-60 s, then steady
    // action with one real climax late in the clip; +-35 % jitter.
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        CAPTURE(seed);
        const double opening = 20 + 5 * seed;
        samn::PhaseSignals noisy;
        noisy.step_ms = 500;
        uint32_t state = seed;
        const auto u = [&] {
            state = state * 1103515245u + 12345u;
            return double((state >> 16) % 1000) / 1000.0;
        };
        for (int i = 0; i < 800; ++i) {
            const double t = i / 2.0;
            const double m = t < opening ? .4 : std::abs(t - 320) < 7 ? .4 : .2;
            const double l = t < opening ? .66 : std::abs(t - 320) < 7 ? .6 : .3;
            noisy.luma.push_back(0.5);
            noisy.motion.push_back(m * (1 + 0.7 * (u() - 0.5)));
            noisy.loudness.push_back(std::min(1.0, l * (1 + 0.7 * (u() - 0.5))));
        }
        const auto mids = ClimaxMids(samn::SuggestPhases(Normalised(noisy)));
        for (double m : mids) CHECK(m > opening + 5);
        CHECK(Near(mids, 320, 5));
    }
}
