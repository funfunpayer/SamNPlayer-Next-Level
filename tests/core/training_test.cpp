#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include "samn/settings.h"
#include "samn/training.h"
#include "samn/training_store.h"

using namespace samn::training;
namespace fs = std::filesystem;

namespace {

// Records every write, in order, per channel.
struct Rec : Device {
    std::vector<double> vib, suc;
    int stops = 0;
    void set_vibration(double v) override { vib.push_back(v); }
    void set_suction(double v) override { suc.push_back(v); }
    void stop() override { ++stops; }
    static double last(const std::vector<double>& v) { return v.empty() ? -1 : v.back(); }
    static double max(const std::vector<double>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
    static bool has(const std::vector<double>& v, double want, double tol = 0.011) {
        return std::any_of(v.begin(), v.end(), [&](double x) { return std::abs(x - want) <= tol; });
    }
};

// Run the session clock by `ms` in 10 ms ticks.
void run(Session& s, double ms) {
    for (double t = 0; t < ms && !s.finished(); t += 10) s.advance(10);
}
// Run to the end (a 1 h cap keeps a bug from hanging the test).
void run_all(Session& s) { run(s, 3600000); }

Options fast(int cycles) {
    Options o;
    o.technique = Technique::StopStart;
    o.channel = Channel::Vibration;
    o.cycles = cycles;
    o.ramp_up_ms = 300;
    o.hold_ms = 2000;
    o.rest_ms = 100;
    o.peak_intensity = 1.0;
    return o;
}

ChannelCurve cv(double start, double peak, double end, int up, int hold, int down) {
    ChannelCurve c;
    c.start_level = start;
    c.peak_level = peak;
    c.end_level = end;
    c.ramp_up_ms = up;
    c.hold_ms = hold;
    c.ramp_down_ms = down;
    return c;
}

Phase ph(const char* name, std::optional<ChannelCurve> v, std::optional<ChannelCurve> s, int repeats, int rest) {
    Phase p;
    p.name = name;
    p.vibration = v;
    p.suction = s;
    p.repeat_cycles = repeats;
    p.rest_ms = rest;
    return p;
}

Script one(std::vector<Phase> phases) {
    Script s;
    s.phases = std::move(phases);
    return s;
}

std::unique_ptr<Session> make(const Options& o, Rec& d) {
    std::string err;
    auto s = Session::custom(o, &d, &err);
    REQUIRE_MESSAGE(s, err);
    return s;
}
std::unique_ptr<Session> make(const Script& sc, Rec& d, Session::Rng rng = {}) {
    std::string err;
    auto s = Session::script(sc, &d, &err, std::move(rng));
    REQUIRE_MESSAGE(s, err);
    return s;
}

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("samn_train_" + std::to_string(::rand()) + "_" + std::to_string(n++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

// ---------------------------------------------------------------- custom form

TEST_CASE("stop_cycle interrupts the hold, zeroes, and the session continues") {
    Rec d;
    auto s = make(fast(2), d);
    std::vector<CycleResult> results;
    s->on_cycle = [&](const CycleResult& r) { results.push_back(r); };
    run(*s, 800);  // 300 ms ramp up, then deep in the 2 s hold
    CHECK(Rec::last(d.vib) == doctest::Approx(1.0));
    s->stop_cycle();
    run(*s, 60);
    CHECK(Rec::last(d.vib) == 0);
    run_all(*s);
    REQUIRE(results.size() == 2);
    CHECK(results[0].stopped_by_user);
    CHECK(results[0].reached_peak_after_ms < 1000);
    CHECK_FALSE(results[1].stopped_by_user);  // the request does not leak into the next cycle
}

TEST_CASE("stop_cycle interrupts the ramp up without reaching the peak") {
    Rec d;
    Options o = fast(1);
    o.ramp_up_ms = 2000;
    auto s = make(o, d);
    run(*s, 500);
    s->stop_cycle();
    run(*s, 100);
    CHECK(Rec::max(d.vib) < 0.5);
    CHECK(Rec::last(d.vib) == 0);
}

TEST_CASE("a session without Interrupt runs as planned") {
    Rec d;
    auto s = make(fast(3), d);
    int cycles = 0;
    s->on_cycle = [&](const CycleResult& r) {
        ++cycles;
        CHECK_FALSE(r.stopped_by_user);
    };
    run_all(*s);
    CHECK(cycles == 3);
    CHECK(Rec::max(d.vib) == doctest::Approx(1.0));
    CHECK(Rec::last(d.vib) == 0);
    CHECK(d.stops == 1);
    CHECK(s->finished());
}

TEST_CASE("invalid requests are refused before the device is touched") {
    Rec d;
    std::string err;
    CHECK_FALSE(Session::custom(Options{}, &d, &err));
    CHECK(err == "at least 1 cycle required");
    CHECK_FALSE(Session::script(Script{}, &d, &err));
    CHECK(err.find("at least 1 phase") != std::string::npos);
    CHECK_FALSE(Session::script(one({ph("empty", std::nullopt, std::nullopt, 1, 0)}), &d, &err));
    CHECK(err.find("no active channel") != std::string::npos);
    CHECK_FALSE(Session::script(one({ph("p", cv(0, 1, 0, 1, 1, 1), std::nullopt, 0, 0)}), &d, &err));
    CHECK(err.find("at least 1 repeat") != std::string::npos);
    CHECK(d.vib.empty());
    CHECK(d.suc.empty());
    CHECK(d.stops == 0);
}

TEST_CASE("ramps write in 50 ms steps and a hold keeps the level") {
    Rec d;
    Options o = fast(1);
    o.ramp_up_ms = 200;
    o.hold_ms = 100;
    o.rest_ms = 100;
    auto s = make(o, d);
    run_all(*s);
    // up: 4 steps to 1.0; down over rest_ms (2 steps) to 0; the rest wait writes nothing.
    std::vector<double> want = {0.25, 0.5, 0.75, 1.0, 0.5, 0.0};
    REQUIRE(d.vib.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) CHECK(d.vib[i] == doctest::Approx(want[i]));
}

TEST_CASE("channel Both sends identical values to vibration and suction") {
    Rec d;
    Options o = fast(1);
    o.channel = Channel::Both;
    auto s = make(o, d);
    run_all(*s);
    CHECK(d.vib == d.suc);
    CHECK_FALSE(d.vib.empty());
}

TEST_CASE("progression raises peak and hold up to the last cycle") {
    Rec d;
    Options o = fast(3);
    o.peak_intensity = 0.5;
    o.progression_per_cycle = 0.4;
    o.hold_ms = 1000;
    auto s = make(o, d);
    std::vector<CycleResult> r;
    s->on_cycle = [&](const CycleResult& c) { r.push_back(c); };
    run_all(*s);
    REQUIRE(r.size() == 3);
    CHECK(r[0].peak_intensity == doctest::Approx(0.5));
    CHECK(r[1].peak_intensity == doctest::Approx(0.6));
    CHECK(r[2].peak_intensity == doctest::Approx(0.7));
    CHECK(r[2].hold_ms == 1400);
}

TEST_CASE("plateau: the next cycle climbs from the plateau, not from 0") {
    Rec d;
    Options o;
    o.technique = Technique::Plateau;
    o.cycles = 2;
    o.ramp_up_ms = 200;
    o.hold_ms = 50;
    o.rest_ms = 200;
    o.peak_intensity = 1.0;
    o.plateau_fraction = 0.7;
    auto s = make(o, d);
    run_all(*s);
    // After the first plateau (0.7) no write drops below it until the second peak.
    size_t i = 0;
    while (i < d.vib.size() && d.vib[i] < 0.99) ++i;
    while (i < d.vib.size() && std::abs(d.vib[i] - 0.7) > 0.011) ++i;
    REQUIRE(i < d.vib.size());
    bool second_peak = false;
    for (size_t j = i + 1; j < d.vib.size(); ++j) {
        if (d.vib[j] >= 0.99) { second_peak = true; break; }
        CHECK(d.vib[j] >= 0.7 - 0.011);
    }
    CHECK(second_peak);
}

TEST_CASE("an Interrupt during the ramp down cuts to 0 and waits the full rest") {
    Rec d;
    Options o = fast(1);
    o.ramp_up_ms = 100;
    o.hold_ms = 100;
    o.rest_ms = 2000;
    auto s = make(o, d);
    std::vector<CycleResult> r;
    s->on_cycle = [&](const CycleResult& c) { r.push_back(c); };
    run(*s, 700);  // in the 2 s ramp down
    REQUIRE(Rec::last(d.vib) > 0.2);
    s->stop_cycle();
    run(*s, 60);
    CHECK(Rec::last(d.vib) == 0);
    CHECK_FALSE(s->finished());  // the full rest (2 s) still runs
    run_all(*s);
    REQUIRE(r.size() == 1);
    CHECK(r[0].stopped_by_user);
    CHECK(r[0].ended_at_ms - r[0].started_at_ms >= 2000 + 700);
}

// ---------------------------------------------------------------- feedback

TEST_CASE("arousal adjusts peak, hold and rest asymmetrically") {
    const double base = 0.8;
    const auto at = adjust_for_arousal(kArousalTarget, base, 4000, 1000);
    CHECK(at.peak == base);
    CHECK(at.hold_ms == 4000);
    CHECK(at.rest_ms == 1000);
    const auto high = adjust_for_arousal(10, base, 4000, 1000);
    CHECK(high.hold_ms < 4000);
    CHECK(high.rest_ms > 1000);
    CHECK(high.peak < base);
    const auto low = adjust_for_arousal(2, base, 4000, 1000);
    CHECK(low.hold_ms > 4000);
    CHECK(low.rest_ms == 1000);
    CHECK(low.peak <= base);
    // Damping up must be stronger than lengthening down.
    CHECK((4000 - high.hold_ms) / 4000.0 > (low.hold_ms - 4000) / 4000.0);
    // Exact 0.7 numbers: arousal 9 -> peak -14 %, rest +40 %.
    const auto nine = adjust_for_arousal(9, 1.0, 1000, 1000);
    CHECK(nine.peak == doctest::Approx(0.86));
    CHECK(nine.rest_ms == 1400);
    CHECK(nine.hold_ms == 700);
    CHECK(adjust_for_arousal(0, base, 4000, 1000).hold_ms == 4000);
}

TEST_CASE("arousal factors express adjust_for_arousal as multipliers") {
    const auto f = arousal_factors(10);
    CHECK(f.peak == doctest::Approx(0.79));
    CHECK(f.hold == doctest::Approx(0.55));
    CHECK(f.rest == doctest::Approx(1.6));
    CHECK(arousal_factors(0).peak == 1.0);
}

TEST_CASE("invalid feedback values are ignored, not clamped") {
    Rec d;
    auto s = make(fast(2), d);
    std::vector<CycleResult> r;
    s->on_cycle = [&](const CycleResult& c) { r.push_back(c); };
    for (int bad : {0, -3, 11, 999}) s->report_arousal(bad);
    run_all(*s);
    REQUIRE(r.size() == 2);
    CHECK(r[0].arousal_before == 0);
}

TEST_CASE("feedback reaches a running session and shapes one cycle only") {
    Rec d;
    Options o = fast(3);
    o.hold_ms = 200;
    auto s = make(o, d);
    std::vector<CycleResult> r;
    s->on_cycle = [&](const CycleResult& c) { r.push_back(c); };
    run(*s, 100);       // inside cycle 0
    s->report_arousal(10);
    run_all(*s);
    REQUIRE(r.size() == 3);
    CHECK(r[0].arousal_before == 0);
    CHECK(r[1].arousal_before == 10);
    CHECK(r[1].peak_intensity < 1.0);
    CHECK(r[1].rest_ms > 100);
    CHECK(r[2].arousal_before == 0);  // consumed
    CHECK(r[2].peak_intensity == doctest::Approx(1.0));
}

// ---------------------------------------------------------------- scripts

TEST_CASE("an untouched channel keeps its level into the next phase, then winds down") {
    Rec d;
    auto s = make(one({ph("Vibration", cv(0.2, 0.8, 0.2, 50, 50, 50), std::nullopt, 1, 10),
                       ph("Suction only", std::nullopt, cv(0, 0.6, 0, 50, 50, 50), 1, 10)}),
                  d);
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& c) { r.push_back(c); };
    run_all(*s);
    REQUIRE(r.size() == 2);
    CHECK(r[0].phase_name == "Vibration");
    CHECK(r[1].phase_name == "Suction only");
    CHECK(Rec::has(d.vib, 0.2));
    CHECK(Rec::max(d.suc) >= 0.55);
    // Natural end with vibration still at 0.2: the closing ramp takes it to 0.
    CHECK(Rec::last(d.vib) == 0);
    CHECK(Rec::last(d.suc) == 0);
}

TEST_CASE("feedback weakens both channels and lengthens the shared rest") {
    Rec d;
    auto s = make(one({ph("Both", cv(0, 0.8, 0, 50, 50, 50), cv(0, 0.6, 0, 50, 50, 50), 1, 1000)}), d);
    s->report_arousal(10);
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& c) { r.push_back(c); };
    run_all(*s);
    REQUIRE(r.size() == 1);
    CHECK(r[0].arousal_before == 10);
    CHECK(r[0].vibration_peak == doctest::Approx(0.8 * 0.79));
    CHECK(r[0].suction_peak == doctest::Approx(0.6 * 0.79));
    CHECK(r[0].rest_ms == 1600);
    CHECK(r[0].peak_factor_applied == doctest::Approx(0.79));
    CHECK(r[0].rest_factor_applied == doctest::Approx(1.6));
    CHECK(Rec::max(d.vib) < 0.8);
}

TEST_CASE("stop_cycle in a script zeroes both channels, also a carried one") {
    Rec d;
    auto s = make(one({ph("Vibration wave", cv(0, 0.5, 0.2, 40, 10, 40), std::nullopt, 1, 10),
                       ph("Suction focus", std::nullopt, cv(0, 1.0, 0, 2000, 2000, 2000), 1, 10)}),
                  d);
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& c) { r.push_back(c); };
    run(*s, 800);
    REQUIRE(Rec::max(d.suc) > 0.05);
    s->stop_cycle();
    run_all(*s);
    REQUIRE(r.size() == 2);
    CHECK(r[1].stopped_by_user);
    CHECK(Rec::last(d.vib) == 0);
    CHECK(Rec::last(d.suc) == 0);
}

TEST_CASE("after an Interrupt the closing ramp is skipped") {
    Rec d;
    auto s = make(one({ph("Long hold", cv(0, 0.8, 0.5, 50, 2000, 50), std::nullopt, 1, 10)}), d);
    run(*s, 100);
    s->stop_cycle();
    run_all(*s);
    CHECK(Rec::last(d.vib) == 0);
    CHECK(d.vib.size() < 12);  // no 2 s wind-down steps
}

TEST_CASE("an earlier Interrupt does not switch off the closing ramp of later repeats") {
    Rec d;
    auto s = make(one({ph("Stopped", cv(0, 0.8, 0, 50, 2000, 50), std::nullopt, 1, 10),
                       ph("Ends elevated", cv(0, 0.8, 0.5, 50, 50, 50), std::nullopt, 1, 10)}),
                  d);
    run(*s, 300);
    s->stop_cycle();
    run_all(*s);
    CHECK(Rec::has(d.vib, 0.5));
    CHECK(Rec::last(d.vib) == 0);
    // The closing ramp winds 0.5 down in 2 s of 50 ms steps.
    CHECK(d.vib.size() > 40);
}

TEST_CASE("an Interrupt during an elevated rest ends the cycle at once") {
    Rec d;
    auto s = make(one({ph("Plateau", cv(0, 0.8, 0.5, 50, 50, 50), cv(0, 0.6, 0.3, 50, 50, 50), 1, 3000)}), d);
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& c) { r.push_back(c); };
    run(*s, 600);
    REQUIRE(Rec::last(d.vib) == doctest::Approx(0.5));
    s->stop_cycle();
    run(*s, 60);
    CHECK(Rec::last(d.vib) == 0);
    CHECK(Rec::last(d.suc) == 0);
    run_all(*s);
    REQUIRE(r.size() == 1);
    CHECK(r[0].stopped_by_user);
    CHECK(r[0].ended_at_ms >= 600 + 3000);  // then the full rest
}

TEST_CASE("start_from_current climbs from the plateau and survives scaling") {
    Rec d;
    ChannelCurve c = cv(0, 0.8, 0.56, 150, 50, 150);
    c.start_from_current = true;
    const Script sc = one({ph("Plateau", c, std::nullopt, 2, 50)});
    auto s = make(sc, d);
    run_all(*s);
    size_t i = 0;
    while (i < d.vib.size() && d.vib[i] < 0.79) ++i;
    while (i < d.vib.size() && std::abs(d.vib[i] - 0.56) > 0.011) ++i;
    REQUIRE(i < d.vib.size());
    for (size_t j = i + 1; j < d.vib.size() && d.vib[j] < 0.79; ++j) CHECK(d.vib[j] >= 0.56 - 0.011);
    CHECK(scale_script(sc, 0.5).phases[0].vibration->start_from_current);
    const auto* plateau = builtin_script("plateau");
    REQUIRE(plateau);
    CHECK(plateau->phases[0].vibration->start_from_current);
}

TEST_CASE("jitter varies the peak per repeat within the fraction, from the injected rng") {
    Rec d;
    ChannelCurve c = cv(0, 0.5, 0, 50, 0, 50);
    c.random_jitter_fraction = 0.25;
    const double seq[] = {0.0, 1.0, 0.5};
    int n = 0;
    auto s = make(one({ph("J", c, std::nullopt, 3, 0)}), d, [&] { return seq[n++ % 3]; });
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& x) { r.push_back(x); };
    run_all(*s);
    REQUIRE(r.size() == 3);
    CHECK(r[0].vibration_peak == doctest::Approx(0.5 * 0.75));
    CHECK(r[1].vibration_peak == doctest::Approx(0.5 * 1.25));
    CHECK(r[2].vibration_peak == doctest::Approx(0.5));
}

TEST_CASE("scaled_curve keeps start and end proportional to the damped peak") {
    const auto f = arousal_factors(10);
    ChannelCurve base = cv(0.8, 0.8, 0.8, 0, 1000, 0);
    const auto got = scaled_curve(base, 1.0, f.peak, f.hold, [] { return 0.5; });
    CHECK(got.start_level == doctest::Approx(got.peak_level));
    CHECK(got.peak_level < 0.8);
    CHECK(got.hold_ms == 550);
}

TEST_CASE("progression spreads over all repeats of the script") {
    Rec d;
    Script sc = one({ph("A", cv(0, 0.5, 0, 50, 0, 50), std::nullopt, 2, 0), ph("B", cv(0, 0.5, 0, 50, 0, 50), std::nullopt, 1, 0)});
    sc.progression_per_cycle = 0.2;
    auto s = make(sc, d);
    std::vector<ScriptCycleResult> r;
    s->on_script_cycle = [&](const ScriptCycleResult& x) { r.push_back(x); };
    run_all(*s);
    REQUIRE(r.size() == 3);
    CHECK(r[0].vibration_peak == doctest::Approx(0.5));
    CHECK(r[1].vibration_peak == doctest::Approx(0.55));
    CHECK(r[2].vibration_peak == doctest::Approx(0.6));
}

TEST_CASE("scale_script scales levels, never timing, clamps, and is a no-op for 0 / 1 / negative") {
    const Script sc = one({ph("p", cv(0.2, 0.6, 0.3, 1000, 2000, 1000), cv(0.1, 0.5, 0.2, 500, 1500, 500), 3, 400)});
    const auto half = scale_script(sc, 0.5);
    const auto& v = *half.phases[0].vibration;
    CHECK(v.start_level == doctest::Approx(0.1));
    CHECK(v.peak_level == doctest::Approx(0.3));
    CHECK(v.end_level == doctest::Approx(0.15));
    CHECK(v.ramp_up_ms == 1000);
    CHECK(v.hold_ms == 2000);
    CHECK(half.phases[0].repeat_cycles == 3);
    CHECK(half.phases[0].rest_ms == 400);
    CHECK(scale_script(sc, 4.0).phases[0].vibration->peak_level == 1.0);
    for (double f : {0.0, -1.0, 1.0}) CHECK(scale_script(sc, f).phases[0].vibration->peak_level == 0.6);
    CHECK(sc.phases[0].vibration->peak_level == 0.6);  // input untouched
}

// ---------------------------------------------------------------- pause / levels

TEST_CASE("pause holds both channels at 0, the clock stands still, resume restores the level") {
    Rec d;
    Options o = fast(1);
    o.ramp_up_ms = 400;
    o.hold_ms = 200;
    o.rest_ms = 100;
    o.channel = Channel::Both;
    auto s = make(o, d);
    run(*s, 200);
    const double clock = s->elapsed_ms();
    s->pause();
    CHECK(s->paused());
    CHECK(Rec::last(d.vib) == 0);
    CHECK(Rec::last(d.suc) == 0);
    CHECK(s->vibration() == 0);
    const size_t writes = d.vib.size() + d.suc.size();
    run(*s, 5000);  // longer than the rest of the session
    CHECK(d.vib.size() + d.suc.size() == writes);
    CHECK(s->elapsed_ms() == clock);
    CHECK_FALSE(s->finished());
    s->resume();
    CHECK_FALSE(s->paused());
    CHECK(Rec::last(d.vib) > 0);
    CHECK(Rec::last(d.vib) == Rec::last(d.suc));
    run_all(*s);
    CHECK(s->finished());
    // 400 + 200 + 100 (ramp down) + 100 (rest) of session time, the pause not counted.
    CHECK(s->elapsed_ms() == doctest::Approx(800).epsilon(0.05));
}

TEST_CASE("pause and resume twice are no-ops, abort ends at once") {
    Rec d;
    auto s = make(fast(3), d);
    s->resume();
    s->pause();
    s->pause();
    s->resume();
    s->resume();
    CHECK_FALSE(s->paused());
    run(*s, 100);
    s->pause();
    s->abort();
    CHECK(s->finished());
    CHECK(d.stops == 1);
    s->abort();
    CHECK(d.stops == 1);
}

TEST_CASE("on_levels reports every write, and 0 while paused") {
    Rec d;
    auto s = make(fast(1), d);
    std::vector<std::pair<double, double>> seen;
    s->on_levels = [&](double v, double c) { seen.emplace_back(v, c); };
    run(*s, 200);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back().first > 0);
    s->pause();
    CHECK(seen.back() == std::make_pair(0.0, 0.0));
    s->resume();
    CHECK(seen.back().first > 0);
}

TEST_CASE("position follows the phase and repeat") {
    Rec d;
    auto s = make(one({ph("A", cv(0, 0.5, 0, 50, 0, 50), std::nullopt, 2, 0), ph("B", std::nullopt, cv(0, 0.5, 0, 50, 0, 50), 1, 0)}), d);
    s->advance(0);
    CHECK(s->position().phase_name == "A");
    CHECK(s->position().phases_total == 2);
    CHECK(s->position().repeats_total == 2);
    run(*s, 120);
    CHECK(s->position().repeat_index == 1);
    run(*s, 100);
    CHECK(s->position().phase_name == "B");
    CHECK(s->position().phase_index == 1);
    CHECK(s->position().vibration_peak == 0);
    CHECK(s->position().suction_peak == doctest::Approx(0.5));
}

TEST_CASE("the custom form reports its cycle and the peak it aims at") {
    Rec d;
    Options o = fast(3);
    o.channel = Channel::Suction;
    o.peak_intensity = 0.6;
    auto s = make(o, d);
    s->advance(0);
    CHECK(s->position().phase_name == "Cycle 1");
    CHECK(s->position().repeats_total == 3);
    CHECK(s->position().vibration_peak == 0);
    CHECK(s->position().suction_peak == doctest::Approx(0.6));
}

// ---------------------------------------------------------------- built-ins

TEST_CASE("built-in scripts are valid, described, categorised and level 1-3") {
    const std::set<std::string> cats = {kCategoryControl, kCategoryEndurance, kCategoryEdge, kCategoryBody};
    for (const auto& sc : builtin_scripts()) {
        INFO(sc.name);
        CHECK(validate_script(sc).empty());
        CHECK_FALSE(sc.description.empty());
        CHECK(cats.count(sc.category) == 1);
        CHECK(sc.level >= 1);
        CHECK(sc.level <= 3);
    }
}

TEST_CASE("every category has a built-in, and the level / body programs exist") {
    std::set<std::string> seen;
    for (const auto& sc : builtin_scripts()) seen.insert(sc.category);
    for (const char* c : {kCategoryControl, kCategoryEndurance, kCategoryEdge, kCategoryBody}) CHECK(seen.count(c) == 1);
    for (const char* n : {"endurance-ladder", "edge-ladder", "breathing-pace", "pelvic-floor"}) CHECK(builtin_script(n));
    const auto* ladder = builtin_script("endurance-ladder");
    REQUIRE(ladder);
    CHECK(ladder->level == 2);
    CHECK(ladder->category == kCategoryEndurance);
    REQUIRE(ladder->phases.size() == 4);
    CHECK(ladder->phases[0].name == "Warm Up");
    CHECK(ladder->phases[3].name == "Cool Down");
    CHECK(builtin_script("edge-ladder")->level == 3);
}

TEST_CASE("no built-in holds suction long at a high level; every built-in drives suction") {
    for (const auto& sc : builtin_scripts()) {
        bool suction = false;
        for (const auto& p : sc.phases) {
            if (!p.suction) continue;
            suction = true;
            INFO(sc.name << "/" << p.name);
            CHECK_FALSE((p.suction->hold_ms > 10000 && p.suction->peak_level >= 0.65));
        }
        INFO(sc.name);
        CHECK(suction);
    }
}

TEST_CASE("built-in lookup and order") {
    CHECK_FALSE(builtin_script("does-not-exist"));
    const auto* w = builtin_script("vibration-wave-suction-focus");
    REQUIRE(w);
    CHECK(w->phases.size() == 2);
    const auto& all = builtin_scripts();
    CHECK(all.size() == 11);
    CHECK(all[0].name == "stop-start");
    CHECK(all[1].name == "plateau");
    CHECK(builtin_script("stop-start")->phases[0].vibration->end_level == 0);
    CHECK(builtin_script("plateau")->phases[0].vibration->end_level > 0);
}

TEST_CASE("the body programs are cue patterns of suction and light vibration") {
    const auto* b = builtin_script("breathing-pace");
    REQUIRE(b);
    CHECK(b->phases[0].repeat_cycles == 10);
    // ~6 breaths a minute: 4.5 s in, 0.5 s hold, 4.5 s out, 0.5 s rest = 10 s a breath.
    const auto& v = *b->phases[0].vibration;
    CHECK(v.ramp_up_ms + v.hold_ms + v.ramp_down_ms + b->phases[0].rest_ms == 10000);
    const auto* p = builtin_script("pelvic-floor");
    REQUIRE(p);
    CHECK(p->phases[0].suction->hold_ms == 2500);
    CHECK(p->phases[0].suction->peak_level > p->phases[0].vibration->peak_level);
}

// ---------------------------------------------------------------- preview

TEST_CASE("preview lists the corners, markers and the total") {
    Script sc = one({ph("Vibration wave", cv(0.2, 0.8, 0.2, 6000, 2000, 6000), std::nullopt, 1, 4000),
                     ph("Suction focus", std::nullopt, cv(0.2, 0.75, 0.3, 5000, 3000, 5000), 1, 5000)});
    const auto p = build_preview(sc);
    CHECK(p.total_ms == 14000 + 4000 + 13000 + 5000);
    REQUIRE(p.markers.size() == 2);
    CHECK(p.markers[1].at_ms == 18000);
    REQUIRE(p.vibration.size() == 5);  // 4 corners + the hold of 0.2 across the second phase
    CHECK(p.vibration[4].level == doctest::Approx(0.2));
    CHECK(p.vibration[4].at_ms == 18000 + 13000);
    CHECK(p.suction.size() == 5);  // 1 hold of 0 across the first phase + 4 corners
    CHECK_FALSE(p.has_random_jitter);
    CHECK(build_preview(*builtin_script("variable")).has_random_jitter);
}

TEST_CASE("phase strength is the mean level of the stronger channel") {
    const Script sc = one({ph("Low", cv(0, 0.4, 0, 1000, 0, 1000), cv(0, 0.2, 0, 1000, 0, 1000), 1, 0),
                           ph("High", cv(0.8, 0.8, 0.8, 1000, 0, 1000), std::nullopt, 1, 0)});
    const auto p = build_preview(sc);
    CHECK(phase_strength(p, 0, 2000) == doctest::Approx(0.2));  // triangle 0 -> 0.4 -> 0
    CHECK(phase_strength(p, 2000, 4000) == doctest::Approx(0.8));
    CHECK(phase_strength(p, 1000, 3000) == doctest::Approx(0.5));  // 0.4 -> 0 for 1 s, then 0.8 for 1 s
    CHECK(phase_strength(p, 100, 100) == 0);
    CHECK(phase_strength(Preview{}, 0, 1000) == 0);
}

TEST_CASE("preview of start_from_current starts at the previous end level") {
    ChannelCurve c = cv(0, 0.8, 0.56, 100, 0, 100);
    c.start_from_current = true;
    const auto p = build_preview(one({ph("P", c, std::nullopt, 2, 0)}));
    CHECK(p.vibration[4].level == doctest::Approx(0.56));
}

TEST_CASE("custom-form preview counts the real cycle length") {
    Options o = fast(2);
    o.ramp_up_ms = 1000;
    o.hold_ms = 500;
    o.rest_ms = 100;
    const auto stop = build_preview(o);
    CHECK(stop.total_ms == 2 * (1000 + 500 + 100 + 100));  // stop-start rests twice
    CHECK(stop.markers.size() == 2);
    CHECK(stop.markers[1].name == "Cycle 2");
    CHECK(stop.suction.empty());
    o.technique = Technique::Plateau;
    o.plateau_fraction = 0.5;
    CHECK(build_preview(o).total_ms == 2 * (1000 + 500 + 100));
    o.channel = Channel::Both;
    CHECK_FALSE(build_preview(o).suction.empty());
}

// ---------------------------------------------------------------- json / store

TEST_CASE("script json round-trips in the 0.7 shape") {
    Script sc = *builtin_script("plateau");
    const nlohmann::json j = sc;
    CHECK(j["phases"][0]["vibration"]["startFromCurrent"] == true);
    CHECK(j["phases"][0]["repeatCycles"] == 5);
    CHECK(j["category"] == "edge");
    const Script back = j.get<Script>();
    CHECK(nlohmann::json(back) == j);
    // A 0.7 file has no category / level and a null channel slot.
    const auto old = nlohmann::json::parse(
        R"({"name":"mine","description":"d","phases":[{"name":"p","vibration":{"channel":"vibration","startLevel":0,"peakLevel":0.5,"endLevel":0,"rampUpMs":10,"holdMs":10,"rampDownMs":10,"randomJitterFraction":0},"suction":null,"repeatCycles":2,"restMs":5}],"progressionPerCycle":0.1})");
    const Script s = old.get<Script>();
    CHECK(s.category.empty());
    CHECK(s.level == 0);
    CHECK(s.phases[0].vibration);
    CHECK_FALSE(s.phases[0].suction);
}

TEST_CASE("script file slugs are safe file names") {
    CHECK(*script_file_slug("My Routine!") == "my-routine");
    CHECK(*script_file_slug("  Abend / kurz ") == "abend-kurz");
    CHECK(*script_file_slug("../../etc/passwd") == "etc-passwd");
    CHECK_FALSE(script_file_slug("   "));
    CHECK_FALSE(script_file_slug("!!!"));
}

TEST_CASE("custom scripts: save, edit in place, collisions, list, find, delete") {
    TempDir t;
    const Script sc = one({ph("p", cv(0, 0.5, 0, 10, 10, 10), std::nullopt, 1, 0)});
    Script a = sc;
    a.name = "My Routine";
    CHECK(save_custom_script(a, t.path).empty());
    a.description = "changed";
    CHECK(save_custom_script(a, t.path).empty());  // same name: edit in place
    CHECK(list_custom_scripts(t.path).size() == 1);
    CHECK(list_custom_scripts(t.path)[0].description == "changed");
    Script b = sc;
    b.name = "my routine?";
    CHECK(save_custom_script(b, t.path).find("same file name") != std::string::npos);
    Script c = sc;
    c.name = "stop-start";
    CHECK(save_custom_script(c, t.path).find("built-in") != std::string::npos);
    c.name = "  ";
    CHECK(save_custom_script(c, t.path) == "script needs a name");
    c.name = "!!!";
    CHECK(save_custom_script(c, t.path).find("letter or number") != std::string::npos);
    CHECK(save_custom_script(Script{"bad", "", "", 0, {}, 0}, t.path).find("at least 1 phase") != std::string::npos);
    CHECK(find_script("My Routine", t.path)->description == "changed");
    CHECK(find_script("plateau", t.path));
    CHECK_FALSE(find_script("nope", t.path));
    CHECK(delete_custom_script("plateau", t.path).find("built-in") != std::string::npos);
    CHECK(delete_custom_script("nope", t.path).find("no custom script") != std::string::npos);
    CHECK(delete_custom_script("My Routine", t.path).empty());
    CHECK(list_custom_scripts(t.path).empty());
}

TEST_CASE("a broken script file does not hide the others") {
    TempDir t;
    Script a = one({ph("p", cv(0, 0.5, 0, 10, 10, 10), std::nullopt, 1, 0)});
    a.name = "good";
    CHECK(save_custom_script(a, t.path).empty());
    std::ofstream(t.path / "bad.json") << "{ nope";
    CHECK(list_custom_scripts(t.path).size() == 1);
}

// ---------------------------------------------------------------- log / history

TEST_CASE("log entries carry the 0.7 fields") {
    Options o = fast(5);
    CycleResult r;
    r.cycle_index = 1;
    r.peak_intensity = 0.9;
    r.hold_ms = 2500;
    r.started_at_ms = 1000;
    r.ended_at_ms = 4000;
    r.stopped_by_user = true;
    r.arousal_before = 8;
    r.rest_ms = 120;
    const auto j = to_json(log_entry(o, r, "2026-01-01T00:00:00Z"));
    CHECK(j["technique"] == "stopstart");
    CHECK(j["channel"] == "vibration");
    CHECK(j["cyclesTotal"] == 5);
    CHECK(j["durationMs"] == 3000);
    CHECK(j["stoppedByUser"] == true);
    CHECK_FALSE(j.contains("phaseName"));
    ScriptCycleResult s;
    s.phase_index = 2;
    s.repeat_index = 3;
    s.phase_name = "Peak";
    s.vibration_peak = 0.4;
    s.suction_peak = 0.7;
    const auto js = to_json(log_entry("endurance-ladder", s, "t"));
    CHECK(js["channel"] == "script");
    CHECK(js["cycleIndex"] == 2003);
    CHECK(js["peakIntensity"] == 0.7);
    CHECK(js["phaseName"] == "Peak");
    CHECK(session_log_name("Abend / kurz", "20260101-101010") == "training-abend-kurz-20260101-101010.jsonl");
    CHECK(session_log_name("???", "x") == "training-session-x.jsonl");
}

namespace {
void write_log(const fs::path& p, const std::string& technique, const std::string& ts, std::vector<bool> stops, std::vector<int> arousal = {}) {
    std::ofstream out(p);
    for (size_t i = 0; i < stops.size(); ++i) {
        nlohmann::json j = {{"timestamp", ts}, {"technique", technique}, {"channel", "vibration"},
                            {"peakIntensity", 0.5 + 0.1 * i}, {"stoppedByUser", bool(stops[i])},
                            {"reachedPeakAfterMs", 1000 * (i + 1)}, {"arousalBefore", i < arousal.size() ? arousal[i] : 0}};
        out << j.dump() << "\n";
    }
}
}  // namespace

TEST_CASE("history: summaries, newest first, broken lines skipped, empty logs ignored") {
    TempDir t;
    write_log(t.path / "training-a.jsonl", "stopstart", "2026-01-01T10:00:00Z", {false, true, false}, {0, 8, 6});
    write_log(t.path / "training-b.jsonl", "plateau", "2026-01-02T10:00:00Z", {false});
    { std::ofstream o(t.path / "training-c.jsonl"); o << "garbage\n"; }
    { std::ofstream o(t.path / "notes.txt"); o << "x"; }
    {
        std::ofstream o(t.path / "training-d.jsonl");
        o << "{ broken\n"
          << nlohmann::json({{"timestamp", "2026-01-03T10:00:00Z"}, {"technique", "x"}, {"peakIntensity", 1.0}}).dump() << "\n";
    }
    const auto h = training_history(t.path);
    REQUIRE(h.size() == 3);
    CHECK(h[0].technique == "x");
    CHECK(h[1].technique == "plateau");
    CHECK(h[2].technique == "stopstart");
    CHECK(h[2].cycles_completed == 3);
    CHECK(h[2].cycles_stopped_early == 1);
    CHECK(h[2].mean_peak_intensity == doctest::Approx(0.6));
    CHECK(h[2].mean_reached_peak_after_ms == doctest::Approx(2000));
    CHECK(h[2].arousal_reports_count == 2);
    CHECK(h[2].mean_arousal_reported == doctest::Approx(7));
    CHECK(training_history(t.path, 2).size() == 2);
    CHECK(training_history(t.path / "missing").empty());
}

TEST_CASE("history csv has the 0.7 header and rows") {
    SessionSummary s;
    s.started_at = "2026-01-01T10:00:00Z";
    s.technique = "a,b";
    s.channel = "vibration";
    s.cycles_completed = 5;
    s.cycles_stopped_early = 1;
    s.mean_peak_intensity = 0.456;
    s.mean_reached_peak_after_ms = 1234.4;
    s.mean_arousal_reported = 6.55;
    s.arousal_reports_count = 2;
    const std::string csv = history_csv({s});
    CHECK(csv.rfind("startedAt,technique,channel,cyclesCompleted,cyclesStoppedEarly,meanPeakIntensityPct,meanReachedPeakAfterMs,meanArousalReported,arousalReportsCount\n", 0) == 0);
    CHECK(csv.find("2026-01-01T10:00:00Z,\"a,b\",vibration,5,1,45.6,1234,") != std::string::npos);
}

TEST_CASE("history adjustment: ease off after an early stop, nudge up after clean sessions") {
    const auto sess = [](const char* key, int stops) {
        SessionSummary s;
        s.technique = key;
        s.cycles_stopped_early = stops;
        return s;
    };
    CHECK(history_adjustment({}, "x").factor == 1);
    CHECK(history_adjustment({sess("x", 0)}, "").factor == 1);
    CHECK(history_adjustment({sess("x", 0)}, "x").factor == 1);  // one clean session is not enough
    const auto stopped = history_adjustment({sess("x", 2), sess("x", 0)}, "x");
    CHECK(stopped.factor == 0.85);
    CHECK(stopped.message.find("2 early stops") != std::string::npos);
    CHECK(history_adjustment({sess("x", 1)}, "x").message.find("1 early stop \xE2\x80\x94") != std::string::npos);
    const auto clean = history_adjustment({sess("x", 0), sess("y", 3), sess("x", 0)}, "x");
    CHECK(clean.factor == 1.1);
    CHECK(clean.message.find("Last 2 sessions") != std::string::npos);
    // Only the newest 3 of this pattern count; an old stop is ignored.
    CHECK(history_adjustment({sess("x", 0), sess("x", 0), sess("x", 0), sess("x", 5)}, "x").factor == 1.1);
    // A stop in the older of two sessions only blocks the nudge.
    CHECK(history_adjustment({sess("x", 0), sess("x", 1)}, "x").factor == 1);
}

TEST_CASE("combined intensity is tier x history, clamped 0.55-1.35, 1 for Custom") {
    SessionSummary s;
    s.technique = "x";
    s.cycles_stopped_early = 1;
    CHECK(combined_intensity_factor(false, 1.2, true, {s}, "x") == 1);
    CHECK(combined_intensity_factor(true, 1.2, false, {s}, "x") == doctest::Approx(1.2));
    CHECK(combined_intensity_factor(true, 1.2, true, {s}, "x") == doctest::Approx(1.02));
    CHECK(combined_intensity_factor(true, 0.8, true, {s}, "x") == doctest::Approx(0.68));
    CHECK(combined_intensity_factor(true, 5, false, {}, "x") == 1.35);
    CHECK(combined_intensity_factor(true, 0.1, false, {}, "x") == 0.55);
}
