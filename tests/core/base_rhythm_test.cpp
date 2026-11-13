#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

#include "samn/base_rhythm.h"
#include "samn/improve.h"
#include "samn/settings.h"

namespace {

bool Same(const std::vector<samn::Action>& a, const std::vector<samn::Action>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].at != b[i].at || a[i].pos != b[i].pos) return false;
    return true;
}

// Stroke script: turning points every half period with amplitude amp around 50.
std::vector<samn::Action> Strokes(int64_t from, int64_t to, double half_ms, int amp) {
    std::vector<samn::Action> a;
    int k = 0;
    for (double t = double(from); t <= double(to); t += half_ms, ++k)
        a.push_back({int64_t(std::llround(t)), 50 + (k % 2 ? amp / 2 : -amp / 2)});
    return a;
}

std::vector<samn::Action> WithGap(std::vector<samn::Action> a, int64_t from, int64_t to) {
    std::vector<samn::Action> out;
    for (const auto& x : a)
        if (x.at <= from || x.at >= to) out.push_back(x);
    return out;
}

std::vector<samn::Action> Inside(const std::vector<samn::Action>& a, int64_t from, int64_t to) {
    std::vector<samn::Action> out;
    for (const auto& x : a)
        if (x.at > from && x.at < to) out.push_back(x);
    return out;
}

}  // namespace

TEST_CASE("base rhythm: defaults and clamping") {
    const samn::BaseRhythm d;
    CHECK(d.bpm == 72);
    CHECK(d.variation_pct == 3);
    samn::BaseRhythm r;
    r.bpm = 20;
    r.variation_pct = 50;
    r = samn::ClampBaseRhythm(r);
    CHECK(r.bpm == 40);
    CHECK(r.variation_pct == 10);
    r.bpm = 300;
    r.variation_pct = -1;
    r = samn::ClampBaseRhythm(r);
    CHECK(r.bpm == 140);
    CHECK(r.variation_pct == 0);
    r.bpm = std::numeric_limits<double>::quiet_NaN();
    r.variation_pct = std::numeric_limits<double>::infinity();
    r = samn::ClampBaseRhythm(r);
    CHECK(r.bpm == 72);
    CHECK(r.variation_pct == 3);
}

TEST_CASE("base rhythm: settings keys, defaults and clamps") {
    const auto dir = std::filesystem::temp_directory_path() / "samn_base_rhythm_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        samn::Settings s(dir / "settings.json");
        CHECK(samn::BaseRhythmFromSettings(s).bpm == 72);
        CHECK(samn::BaseRhythmFromSettings(s).variation_pct == 3);
        CHECK_FALSE(samn::RhythmicFillFromSettings(s));
        s.set("generator.base_bpm", 64);
        s.set("generator.base_bpm_variation", 5.5);
        s.set("generator.rhythmic_fill", true);
        CHECK(samn::BaseRhythmFromSettings(s).bpm == 64);
        CHECK(samn::BaseRhythmFromSettings(s).variation_pct == 5.5);
        CHECK(samn::RhythmicFillFromSettings(s));
        s.set("generator.base_bpm", 500);
        s.set("generator.base_bpm_variation", "lots");
        s.set("generator.rhythmic_fill", "yes");
        CHECK(samn::BaseRhythmFromSettings(s).bpm == 140);
        CHECK(samn::BaseRhythmFromSettings(s).variation_pct == 3);
        CHECK_FALSE(samn::RhythmicFillFromSettings(s));
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("base rhythm: a deterministic grid that drifts slowly and never jitters") {
    const samn::BaseRhythm r;
    const auto a = samn::BaseBeatGrid(r, 0, 600000);  // 10 minutes
    const auto b = samn::BaseBeatGrid(r, 0, 600000);
    CHECK(a == b);
    REQUIRE(a.size() > 600);
    CHECK(a.front() == 0);
    double lo = 1e9, hi = 0, worst_step = 0;
    for (std::size_t k = 1; k < a.size(); ++k) {
        const double interval = a[k] - a[k - 1];
        const double bpm = 60000.0 / interval;
        lo = std::min(lo, bpm);
        hi = std::max(hi, bpm);
        if (k >= 2) worst_step = std::max(worst_step, std::fabs(interval / (a[k - 1] - a[k - 2]) - 1));
    }
    // Within +-3 % of 72, actually varying, and smooth: adjacent intervals
    // differ by well under 1 % (drift, not jitter).
    CHECK(lo >= 72 * 0.97 - 1e-9);
    CHECK(hi <= 72 * 1.03 + 1e-9);
    CHECK(hi - lo >= 1.0);
    CHECK(worst_step < 0.01);
    // Mean tempo near the base.
    const double mean_bpm = 60000.0 * double(a.size() - 1) / (a.back() - a.front());
    CHECK(mean_bpm == doctest::Approx(72).epsilon(0.03));
    // Another seed is another (equally valid) drift; no variation is a metronome.
    samn::BaseRhythm other;
    other.seed = 42;
    CHECK(samn::BaseBeatGrid(other, 0, 600000) != a);
    samn::BaseRhythm steady;
    steady.variation_pct = 0;
    const auto m = samn::BaseBeatGrid(steady, 1000, 11000);
    for (std::size_t k = 1; k < m.size(); ++k) CHECK(m[k] - m[k - 1] == doctest::Approx(60000.0 / 72));
    CHECK(samn::BaseBeatGrid(r, 5, 5).empty());
    CHECK(samn::BaseBeatGrid(r, 0, std::numeric_limits<double>::infinity()).empty());
    for (double t = -50000; t < 50000; t += 777) CHECK(std::fabs(samn::BaseRhythmDrift(r, t)) <= 1.0);
}

TEST_CASE("base rhythm: adjacent beats differ by under 1 % over the whole clamped range") {
    // The drift's knots are spaced in beats, so the bound does not depend
    // on the tempo or the variation.
    struct Case {
        double bpm, pct;
    };
    for (const Case c : {Case{40, 10}, Case{140, 10}, Case{72, 10}, Case{40, 3}, Case{72, 3}, Case{140, 0.5}}) {
        double worst = 0, lo = 1e9, hi = 0;
        for (std::uint64_t seed = 1; seed <= 60; ++seed) {
            samn::BaseRhythm r;
            r.bpm = c.bpm;
            r.variation_pct = c.pct;
            r.seed = seed * 7919;
            const auto g = samn::BaseBeatGrid(r, -30000, 1200000);  // 20 minutes
            for (std::size_t k = 2; k < g.size(); ++k) {
                const double interval = g[k] - g[k - 1];
                worst = std::max(worst, std::fabs(interval / (g[k - 1] - g[k - 2]) - 1));
                lo = std::min(lo, 60000.0 / interval);
                hi = std::max(hi, 60000.0 / interval);
            }
        }
        CAPTURE(c.bpm);
        CAPTURE(c.pct);
        CHECK(worst < 0.01);
        CHECK(lo >= c.bpm * (1 - c.pct / 100) - 1e-6);
        CHECK(hi <= c.bpm * (1 + c.pct / 100) + 1e-6);
    }
}

TEST_CASE("base rhythm: huge or overflowing times stay defined") {
    const samn::BaseRhythm r;
    for (double t : {1e23, -1e23, 1e300, -1e300}) {
        CHECK(std::isfinite(samn::BaseRhythmDrift(r, t)));
        CHECK(std::fabs(samn::BaseRhythmDrift(r, t)) <= 1.0);
    }
    // end - start overflows to infinity: nothing, instead of a giant reserve.
    CHECK(samn::BaseBeatGrid(r, -1.7e308, 1.7e308).empty());
    // A long but finite range stops at the cap.
    CHECK(samn::BaseBeatGrid(r, 0, 1e15).size() <= samn::kBaseBeatGridMaxBeats);
}

TEST_CASE("rhythmic fill: continues the script's own strokes through a gap") {
    const auto ref = Strokes(0, 30000, 400, 60);  // 75 strokes per minute
    const auto gapped = WithGap(ref, 12000, 16000);
    int gaps = 0, pts = 0;
    const auto out = samn::FillGapsRhythmic(gapped, 800, 0, 0, samn::BaseRhythm{}, &gaps, &pts);
    CHECK(gaps == 1);
    const auto in = Inside(out, 12000, 16000);
    REQUIRE(in.size() == static_cast<std::size_t>(pts));
    CHECK(in.size() == 9);  // 4 s at a 400 ms half period, the last 400 ms clear of the anchor
    for (std::size_t k = 0; k < in.size(); ++k) {
        CHECK(in[k].at == 12000 + 400 * int64_t(k + 1));
        CHECK(in[k].pos == ref[std::size_t(30 + k + 1)].pos);  // in phase with the reference
    }
    // The points outside the gap are untouched.
    CHECK(Same(Inside(out, -1, 12001), Inside(gapped, -1, 12001)));
}

TEST_CASE("rhythmic fill: where the script holds, the output is exactly FillGaps") {
    // Steps that do not divide the gap (37 ms, or half the period of a
    // 1.2 Hz audio tempo, 417 ms), and gaps too short for a stroke: the
    // straight-line branch must add the same points as FillGaps.
    std::vector<samn::Action> hold;
    for (int64_t t = 0; t <= 20000; t += 200) hold.push_back({t, 40 + int(t / 200 % 2)});
    struct Case {
        int64_t from, to, max_gap, step;
        double hz;
    };
    for (const Case c : {Case{3000, 7000, 800, 37, 0}, Case{3000, 7000, 800, 0, 1.2}, Case{2000, 2777, 500, 0, 0},
                         Case{5000, 5400, 300, 37, 0}, Case{4000, 12345, 800, 100, 0}, Case{1000, 1900, 800, 333, 0},
                         Case{6000, 9001, 800, 0, 2.7}}) {
        CAPTURE(c.from);
        CAPTURE(c.to);
        CAPTURE(c.step);
        CAPTURE(c.hz);
        const auto in = WithGap(hold, c.from, c.to);
        int g1 = 0, p1 = 0, g2 = 0, p2 = 0;
        const auto rhythmic = samn::FillGapsRhythmic(in, c.max_gap, c.step, c.hz, samn::BaseRhythm{}, &g1, &p1);
        const auto linear = samn::FillGaps(in, c.max_gap, c.step, c.hz, &g2, &p2);
        CHECK(Same(rhythmic, linear));
        CHECK(g1 == g2);
        CHECK(p1 == p2);
    }
}

TEST_CASE("rhythmic fill: a long line step does not stop the strokes") {
    // Review of the follow-up: FillGaps' "gap longer than the step" rule is
    // for the straight line only. A 700 ms gap in a script stroking every
    // 250 ms is stroked even when the line step (714 ms from a 0.7 Hz audio
    // tempo, or 1000 ms) is longer than the gap.
    std::vector<samn::Action> a;
    int k = 0;
    for (int64_t t = 0; t <= 5000; t += 250, ++k) a.push_back({t, k % 2 ? 80 : 20});
    for (int64_t t = 5700; t <= 11000; t += 250, ++k) a.push_back({t, k % 2 ? 80 : 20});
    struct Case {
        int64_t step;
        double hz;
    };
    for (const Case c : {Case{0, 0.7}, Case{1000, 0}}) {
        CAPTURE(c.step);
        CAPTURE(c.hz);
        int gaps = 0, pts = 0;
        const auto out = samn::FillGapsRhythmic(a, 450, c.step, c.hz, samn::BaseRhythm{}, &gaps, &pts);
        CHECK(gaps == 1);
        CHECK(pts == 2);
        const auto in = Inside(out, 5000, 5700);
        REQUIRE(in.size() == 2);
        CHECK(in[0].at == 5250);
        CHECK(in[1].at == 5500);
        CHECK(in[0].pos > in[1].pos);  // a stroke: up from the low anchor, then back
    }
}

TEST_CASE("rhythmic fill: holds where the script holds, heart rhythm without strokes") {
    // A script holding at 30 around the gap: a straight line as in FillGaps.
    std::vector<samn::Action> hold;
    for (int64_t t = 0; t <= 10000; t += 500) hold.push_back({t, 30 + int(t / 500 % 2)});
    const auto held = samn::FillGapsRhythmic(WithGap(hold, 3000, 7000), 800, 0, 0, samn::BaseRhythm{});
    for (const auto& x : Inside(held, 3000, 7000)) CHECK((x.pos >= 30 && x.pos <= 31));
    // The straight line takes the caller's spacing, as FillGaps does.
    const auto spaced = Inside(samn::FillGapsRhythmic(WithGap(hold, 3000, 7000), 800, 250, 0, samn::BaseRhythm{}), 3000, 7000);
    REQUIRE(spaced.size() >= 2);
    for (std::size_t k = 1; k < spaced.size(); ++k) CHECK(spaced[k].at - spaced[k - 1].at == 250);
    CHECK(Same(samn::FillGapsRhythmic(WithGap(hold, 3000, 7000), 800, 250, 0, samn::BaseRhythm{}),
               samn::FillGaps(WithGap(hold, 3000, 7000), 800, 250, 0)));

    // One stroke on either side, too few for a tempo: the heart rhythm strokes it.
    std::vector<samn::Action> sparse{{0, 10}, {900, 90}, {20000, 50}, {20500, 10}};
    samn::BaseRhythm steady;
    steady.variation_pct = 0;
    const auto out = samn::FillGapsRhythmic(sparse, 800, 0, 0, steady);
    const auto in = Inside(out, 900, 20000);
    REQUIRE(in.size() >= 40);
    // Half a beat apart at 72 BPM (417 ms), alternating sides.
    for (std::size_t k = 1; k < in.size(); ++k) {
        CHECK(double(in[k].at - in[k - 1].at) == doctest::Approx(30000.0 / 72).epsilon(0.01));
        CHECK((in[k].pos - in[k - 1].pos) * (k % 2 ? 1 : -1) > 0);
    }
    for (const auto& x : out) CHECK((x.pos >= 0 && x.pos <= 100));
    // An audio tempo wins over the heart rhythm (1.5 Hz = 90 BPM).
    const auto audio = Inside(samn::FillGapsRhythmic(sparse, 800, 0, 1.5, steady), 900, 20000);
    REQUIRE(audio.size() >= 2);
    CHECK(std::fabs(double(audio[1].at - audio[0].at) - 1000.0 / 3) <= 1);  // whole ms
    // Deterministic.
    CHECK(Same(samn::FillGapsRhythmic(sparse, 800, 0, 0, samn::BaseRhythm{}),
               samn::FillGapsRhythmic(sparse, 800, 0, 0, samn::BaseRhythm{})));
}

TEST_CASE("rhythmic fill: off by default in ImproveScript, on by the option") {
    const auto gapped = WithGap(Strokes(0, 30000, 400, 60), 12000, 16000);
    samn::ImproveOptions o;
    o.fill_gaps = true;
    CHECK_FALSE(o.rhythmic_fill);
    const auto linear = samn::ImproveScript(gapped, o);
    CHECK(Same(linear.actions, samn::FillGaps(gapped, linear.fill_gap_ms, 0, 0)));
    o.rhythmic_fill = true;
    const auto rhythmic = samn::ImproveScript(gapped, o);
    CHECK(rhythmic.gaps_filled == 1);
    CHECK(rhythmic.points_added == 9);
    CHECK_FALSE(Same(rhythmic.actions, linear.actions));
}
