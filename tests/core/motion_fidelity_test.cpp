#include <doctest/doctest.h>

#include <cmath>
#include <random>
#include <set>

#include "samn/motion_fidelity.h"

using namespace samn;

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<Action> Sine(int duration_ms, int period_ms, int step_ms, int64_t t0, double amp, double center) {
    std::vector<Action> out;
    for (int t = 0; t <= duration_ms; t += step_ms)
        out.push_back({t0 + t, int(std::lround(center + amp * std::sin(2 * kPi * t / period_ms)))});
    return out;
}

std::vector<Action> Irregular(int64_t t0) {
    std::vector<Action> out;
    int t = 0;
    for (; t < 4000; t += 40) out.push_back({t0 + t, int(std::lround(50 + 15 * std::sin(2 * kPi * t / 500)))});
    for (; t < 6000; t += 40) out.push_back({t0 + t, 50});
    for (; t < 12000; t += 40) out.push_back({t0 + t, int(std::lround(50 + 45 * std::sin(2 * kPi * t / 2000)))});
    return out;
}

// Non-periodic chirp with a one-shot bump: no half-period ambiguity.
std::vector<Action> Chirp(int duration_ms, int step_ms, int64_t t0) {
    std::vector<Action> out;
    for (int t = 0; t <= duration_ms; t += step_ms) {
        const double f = 0.0004 + 0.0000008 * t;
        double pos = 50 + 35 * std::sin(2 * kPi * f * t);
        if (t > 8000 && t < 10000) pos += 20;
        out.push_back({t0 + t, int(std::lround(pos))});
    }
    return out;
}

// Alternates low/high with a fresh random half-cycle each time: stroke-like
// but never repeating.
std::vector<Action> IrregularHalfCycles(int duration_ms, unsigned seed, int min_half, int max_half, int64_t t0, int low,
                                        int high) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, max_half - min_half - 1);
    std::vector<Action> out;
    bool is_low = true;
    for (int at = 0; at <= duration_ms; at += min_half + d(rng), is_low = !is_low)
        out.push_back({t0 + at, is_low ? low : high});
    return out;
}
}  // namespace

// Ported from 0.7 funscript/phase_test.go.
TEST_CASE("fidelity: a shifted sine is found at its lag") {
    const auto r = BestLagCorrelation(Sine(20000, 2000, 40, 0, 40, 50), Sine(20000, 2000, 40, 837, 40, 50), 2000, 50, 100);
    REQUIRE(r);
    CHECK(std::abs(r->lag_ms + 837) <= 50);
    CHECK(r->r > 0.95);
    REQUIRE(r->r_zero_lag);
    CHECK(r->r - *r->r_zero_lag > 0.3);
    CHECK(!r->low_confidence);
}

TEST_CASE("fidelity: an inverted script is found as inverted") {
    auto ref = Sine(20000, 2000, 40, 0, 40, 50), inv = ref;
    for (Action& a : inv) a.pos = 100 - a.pos;
    const auto r = BestLagCorrelation(ref, inv, 200, 50, 100);
    REQUIRE(r);
    CHECK(r->orientation == "inverted");
    CHECK(r->r > 0.95);
}

TEST_CASE("fidelity: a constant series is undefined, not 0") {
    std::vector<Action> flat;
    for (int t = 0; t < 5000; t += 100) flat.push_back({t, 100});
    CHECK(!BestLagCorrelation(Sine(20000, 2000, 40, 0, 40, 50), flat, 500, 100, 100));
    CHECK(!Pearson({1, 1, 1, 1}, {1, 2, 3, 4}));
}

TEST_CASE("fidelity: irregular motion matches with a small shape error") {
    const auto r = BestLagCorrelation(Irregular(0), Irregular(250), 1000, 50, 100);
    REQUIRE(r);
    CHECK(r->r > 0.9);
    REQUIRE(r->shape_error);
    CHECK(*r->shape_error < 0.3);
}

TEST_CASE("fidelity: a short overlap is low confidence") {
    const auto r = BestLagCorrelation(Sine(2000, 500, 40, 0, 40, 50), Sine(2000, 500, 40, 50, 40, 50), 1000, 50, 100);
    REQUIRE(r);
    CHECK(r->low_confidence);
}

TEST_CASE("fidelity: diagnosis tells timing from shape") {
    const auto ref = Sine(20000, 2000, 40, 0, 40, 50);
    CHECK(DiagnosePhase(BestLagCorrelation(ref, Sine(20000, 2000, 40, 837, 40, 50), 2000, 50, 100)).verdict ==
          PhaseVerdict::kTiming);
    std::vector<Action> noise;
    for (int i = 0; i < 200; ++i) noise.push_back({i * 100, (i * 17) % 101});
    const PhaseVerdict v = DiagnosePhase(BestLagCorrelation(ref, noise, 500, 100, 100)).verdict;
    CHECK((v == PhaseVerdict::kShape || v == PhaseVerdict::kUndefined));
    CHECK(DiagnosePhase(std::nullopt).verdict == PhaseVerdict::kUndefined);
    CHECK(std::string(PhaseVerdictName(PhaseVerdict::kTiming)) == "timing");
}

TEST_CASE("fidelity: windows find a constant lag on non-periodic motion") {
    const auto w = WindowedBestLagCorrelation(Chirp(60000, 50, 0), Chirp(60000, 50, 500), 15000, 2000, 50, 100);
    REQUIRE(w.size() >= 3);
    int confident = 0;
    for (const auto& row : w) {
        if (!row.correlation) continue;
        ++confident;
        CHECK(std::abs(row.correlation->lag_ms + 500) <= 100);
        CHECK(row.correlation->r >= 0.9);
    }
    CHECK(confident >= 3);
}

TEST_CASE("fidelity: windows see a lag that changes mid-clip") {
    const auto ref = Chirp(40000, 40, 0);
    auto cand = ref;
    for (Action& a : cand)
        if (a.at >= 20000) a.at += 800;
    const auto w = WindowedBestLagCorrelation(ref, cand, 10000, 2000, 50, 100);
    REQUIRE(w.size() >= 3);
    std::optional<int> early, late;
    for (const auto& row : w) {
        if (!row.correlation) continue;
        if (row.end_ms <= 15000) early = row.correlation->lag_ms;
        if (row.start_ms >= 25000) late = row.correlation->lag_ms;
    }
    REQUIRE(early);
    REQUIRE(late);
    CHECK(std::abs(*early) <= 150);
    CHECK(std::abs(*late + 800) <= 150);
}

// GD-06: a constant lag on periodic motion can alias onto lag +- k x period
// and swing across windows; non-periodic motion recovers it everywhere.
TEST_CASE("fidelity: periodicity aliasing characterisation") {
    const int period = 280, true_lag = 300;
    const auto ref = Sine(60000, period, 20, 0, 35, 55), shifted = Sine(60000, period, 20, true_lag, 35, 55);

    SUBCASE("whole clip: an aliased lag is whole periods away and flagged") {
        const auto r = BestLagCorrelation(ref, shifted, 1500, 20, 20);
        REQUIRE(r);
        CHECK(r->r >= 0.99);
        // Deterministic here: -860 = -300 - 2 x 280 (0.7's Go gave the same kind of answer).
        CHECK(std::abs(r->lag_ms + true_lag) >= period / 2);
        CHECK((r->lag_ms + true_lag) % period == 0);
        CHECK(r->aliasing_risk);
        CHECK(r->dominant_period_ms == period);
        CHECK(!r->alternate_lags_ms.empty());
    }
    SUBCASE("windows: the lag swings and the orientation flips despite a constant offset") {
        std::set<int> lags;
        std::set<std::string> orientations;
        for (const auto& row : WindowedBestLagCorrelation(ref, shifted, 10000, 1500, 20, 20))
            if (row.correlation) {
                lags.insert(row.correlation->lag_ms);
                orientations.insert(row.correlation->orientation);
            }
        CHECK(lags.size() >= 2);
        CHECK(orientations.size() == 2);
    }
    SUBCASE("non-periodic motion: every window finds the true lag, no flag") {
        const auto w = WindowedBestLagCorrelation(IrregularHalfCycles(60000, 42, 130, 450, 0, 20, 90),
                                                  IrregularHalfCycles(60000, 42, 130, 450, true_lag, 20, 90), 10000,
                                                  1500, 20, 20);
        int confident = 0;
        for (const auto& row : w) {
            if (!row.correlation) continue;
            ++confident;
            CHECK(std::abs(row.correlation->lag_ms + true_lag) <= 20);
            CHECK(row.correlation->orientation == "normal");
            CHECK(!row.correlation->aliasing_risk);
        }
        CHECK(confident >= 3);
    }
}

TEST_CASE("fidelity: the aliasing flag never changes the reported lag") {
    const auto r = BestLagCorrelation(IrregularHalfCycles(40000, 7, 140, 420, 0, 20, 90),
                                      IrregularHalfCycles(40000, 7, 140, 420, 400, 20, 90), 1500, 20, 20);
    REQUIRE(r);
    CHECK(std::abs(r->lag_ms + 400) <= 40);
    CHECK(!r->aliasing_risk);
}

TEST_CASE("fidelity: dominant period needs a genuine local peak above the floor") {
    CHECK(std::abs(DominantPeriodMs(Sine(60000, 280, 20, 0, 35, 55), 20) - 280) <= 40);
    const auto fast = Sine(60000, 100, 20, 0, 35, 55);  // true period under the 150 ms floor
    for (int step : {100, 40, 20, 30}) {
        const int p = DominantPeriodMs(fast, step);
        CHECK((p == 0 || p >= kMinDominantPeriodMs));
    }
    // The round-up of the floor cannot be seen from outside: a peak counts
    // only from the second searched lag on, so the smallest period reported
    // is (floor / step + 1) x step >= 150 ms anyway. It stays as a guard.
    const int p = DominantPeriodMs(Sine(60000, 120, 40, 0, 35, 55), 40);
    CHECK((p == 0 || p >= kMinDominantPeriodMs));
}
