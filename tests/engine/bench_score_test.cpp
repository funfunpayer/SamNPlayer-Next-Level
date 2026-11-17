// The detection benchmark's own arithmetic (engine/tools/bench_score.h): the
// peak metrics that can move, the presets and --set, the noise-draw median
// and the acceptance rule on two CSVs. No video is read here.

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "bench_score.h"

using namespace samn::bench;
using samn::Action;

namespace {
constexpr double kPi = 3.14159265358979323846;

Truth Sine(double hz, int seconds, double fps = 25) {
    Truth t;
    t.fps = fps;
    t.frames = int(fps * seconds);
    t.signal = [hz](double s) { return 40 * std::sin(2 * kPi * hz * s); };
    return t;
}

// A script that samples the truth every step_ms, moved by shift_ms.
std::vector<Action> Sampled(const Truth& t, int step_ms, int shift_ms = 0, double gain = 1.0) {
    std::vector<Action> a;
    const int end = int((t.frames - 1) * 1000.0 / t.fps);
    for (int ms = 0; ms <= end; ms += step_ms)
        a.push_back({ms + shift_ms, int(std::lround(50 + gain * 50 * t.signal(ms / 1000.0) / 40))});
    return a;
}

BenchTable Table(const std::string& csv) {
    std::string error;
    const auto t = ParseBenchCsv("scene,metric,value\n" + csv, &error);
    REQUIRE_MESSAGE(t, error);
    return *t;
}
}  // namespace

TEST_CASE("bench: the sub-millisecond truth finds a peak between two grid points") {
    // 0.97 Hz: the first maximum is at 1000 / (4 * 0.97) = 257.73 ms.
    const Truth t = Sine(0.97, 12);
    const auto grid = TruthKeys(t);
    const auto sub = TruthKeysSubMs(t);
    REQUIRE(grid.size() == sub.size());
    REQUIRE(grid.size() > 20);
    CHECK(grid[0].t_ms == doctest::Approx(258).epsilon(1e-9));  // the first metric: whole milliseconds
    CHECK(sub[0].t_ms == doctest::Approx(257.732).epsilon(1e-5));
    CHECK(sub[0].kind == 1);
    for (size_t i = 0; i < sub.size(); ++i) CHECK(std::abs(sub[i].t_ms - grid[i].t_ms) <= 0.5 + 1e-9);
}

TEST_CASE("bench: at 1 Hz and 25 fps the peak error sits on the 10 ms frame-grid floor") {
    // The saturation the first baseline showed: every true peak is 10 ms from a frame.
    const Truth t = Sine(1.0, 12);
    const auto keys = TruthKeysSubMs(t);
    std::vector<double> frames;
    for (int i = 0; i < t.frames; ++i) frames.push_back(i * 40.0);
    CHECK(PeakFloorMs(keys, frames) == doctest::Approx(10.0));
    const ScriptScore s = Score(Sampled(t, 40), t, keys);
    CHECK(s.peak_med == doctest::Approx(10.0));
    CHECK(s.r_median > 0.99);
}

TEST_CASE("bench: the floor rises with a lower analysis rate, and the peak error follows") {
    const Truth t = Sine(1.0, 12);
    const auto keys = TruthKeysSubMs(t);
    std::vector<double> every_frame, every_second;
    for (int i = 0; i < t.frames; ++i) every_frame.push_back(i * 40.0);
    for (int i = 0; i < t.frames; i += 2) every_second.push_back(i * 40.0);
    const double floor25 = PeakFloorMs(keys, every_frame), floor12 = PeakFloorMs(keys, every_second);
    CHECK(floor12 > floor25 + 5);
    const ScriptScore s25 = Score(Sampled(t, 40), t, keys), s12 = Score(Sampled(t, 80), t, keys);
    CHECK(s12.peak_med >= floor12 - 1e-9);
    CHECK(s12.peak_med > s25.peak_med + 5);
}

TEST_CASE("bench: the peak bias has a sign, the peak error does not") {
    const Truth t = Sine(0.97, 12);
    const auto keys = TruthKeysSubMs(t);
    const ScriptScore late = Score(Sampled(t, 5, +30), t, keys), early = Score(Sampled(t, 5, -30), t, keys);
    CHECK(late.peak_bias == doctest::Approx(30).epsilon(0.15));
    CHECK(early.peak_bias == doctest::Approx(-30).epsilon(0.15));
    CHECK(late.peak_med == doctest::Approx(early.peak_med).epsilon(0.2));
    // A script off the frame grid can go below the floor: the metric can move.
    const ScriptScore fine = Score(Sampled(t, 5), t, keys);
    CHECK(fine.peak_med < 5);
}

TEST_CASE("bench: the amplitude ratio follows the script's swing") {
    const Truth t = Sine(1.0, 12);
    const auto keys = TruthKeys(t);
    CHECK(Score(Sampled(t, 40, 0, 1.0), t, keys).amp_ratio == doctest::Approx(1.0).epsilon(0.03));
    CHECK(Score(Sampled(t, 40, 0, 0.5), t, keys).amp_ratio == doctest::Approx(0.5).epsilon(0.06));
}

TEST_CASE("bench: the default preset asks for the Recommended profile and nothing else") {
    BenchConfig c;
    std::string error;
    REQUIRE(ApplyPreset("default", &c, &error));
    CHECK(c.signal.profile == "recommended");
    CHECK(c.signal.smooth_window == 0);
    CHECK(c.signal.max_gap_ms == 0);
    CHECK(c.signal.outlier_mad_factor == 0);
    CHECK(c.signal.adaptive_error == 0);
    CHECK(c.signal.dynamic_range_ms == 0);
    CHECK(c.max_analysis_fps == 0);
    CHECK(c.analysis_width == 0);
    CHECK(c.camera_compensation);
    CHECK(c.scene_cuts);
    CHECK_FALSE(c.adaptive_cuts);
    CHECK_FALSE(c.auto_ozone);
    CHECK_FALSE(c.contact_vibration);
    CHECK(c.axis == "scene");
}

TEST_CASE("bench: the app preset carries Everyday Create's switches, old the same without the speed-ups") {
    BenchConfig app, old;
    std::string error;
    REQUIRE(ApplyPreset("app", &app, &error));
    REQUIRE(ApplyPreset("old", &old, &error));
    for (const BenchConfig* c : {&app, &old}) {
        CHECK(c->signal.profile == "recommended");
        CHECK(c->signal.smooth_window == 11);
        CHECK(c->signal.min_peak_distance_ms == 150);
        CHECK(c->signal.dynamic_range_ms == 3000);
        CHECK(c->signal.adaptive_error == 6);
        CHECK(c->signal.max_gap_ms == 500);
        CHECK(c->signal.outlier_mad_factor == 5);
        CHECK(c->signal.min_action_interval_ms == 0);  // the profile's 60 ms; a stroker gets 100 through --set
        CHECK(c->camera_compensation);
        CHECK(c->scene_cuts);
        CHECK_FALSE(c->adaptive_cuts);  // Generate's fixed cut test
        CHECK(c->auto_ozone);
    }
    CHECK(app.max_analysis_fps == 15);
    CHECK(app.analysis_width == 640);
    CHECK(old.max_analysis_fps == 0);
    CHECK(old.analysis_width == 0);
    BenchConfig none;
    CHECK_FALSE(ApplyPreset("fast", &none, &error));
    CHECK(error.find("fast") != std::string::npos);
}

TEST_CASE("bench: --set changes one value; an unknown key or a bad value is an error") {
    BenchConfig c;
    std::string error;
    REQUIRE(ApplyPreset("app", &c, &error));
    REQUIRE(ApplySet("max_analysis_fps=0", &c, &error));
    REQUIRE(ApplySet("analysis_width=320", &c, &error));
    REQUIRE(ApplySet("smooth_window=7", &c, &error));
    REQUIRE(ApplySet("peak_prominence=0.15", &c, &error));
    REQUIRE(ApplySet("adaptive_cuts=1", &c, &error));
    REQUIRE(ApplySet("min_action_interval_ms=100", &c, &error));
    REQUIRE(ApplySet("axis=auto", &c, &error));
    CHECK(c.max_analysis_fps == 0);
    CHECK(c.analysis_width == 320);
    CHECK(c.signal.smooth_window == 7);
    CHECK(c.signal.peak_prominence == doctest::Approx(0.15));
    CHECK(c.adaptive_cuts);
    CHECK(c.signal.min_action_interval_ms == 100);
    CHECK(c.axis == "auto");
    CHECK(c.overrides.size() == 7);
    // Every documented key is accepted.
    for (const std::string& key : SetKeys()) {
        BenchConfig k;
        const std::string value = key == "profile" ? "standard" : key == "axis" ? "x" : "1";
        CHECK_MESSAGE(ApplySet(key + "=" + value, &k, &error), key);
    }
    const BenchConfig before = c;
    for (const char* bad : {"smoth_window=7", "coast_frames=3", "smooth_window", "smooth_window=", "=7",
                            "smooth_window=seven", "axis=diagonal", "max_gap_ms=1e999"}) {
        error.clear();
        CHECK_FALSE_MESSAGE(ApplySet(bad, &c, &error), bad);
        CHECK_FALSE(error.empty());
    }
    CHECK(c.signal.smooth_window == before.signal.smooth_window);
    CHECK(c.overrides.size() == before.overrides.size());
}

TEST_CASE("bench: the configuration prints every value in a fixed order") {
    BenchConfig c;
    std::string error;
    REQUIRE(ApplyPreset("app", &c, &error));
    const auto lines = ConfigLines(c);
    REQUIRE(lines.size() > 20);
    CHECK(lines[0].first == "preset");
    CHECK(lines[0].second == "app");
    bool fps = false, width = false;
    for (const auto& kv : lines) {
        fps = fps || (kv.first == "max_analysis_fps" && kv.second == "15");
        width = width || (kv.first == "analysis_width" && kv.second == "640");
    }
    CHECK(fps);
    CHECK(width);
}

TEST_CASE("bench: noise draws give the median and the spread") {
    const std::vector<Rows> draws = {{{"gen_ok", 1}, {"r_median", 0.90}, {"track_r_median", 0.80}, {"extrema_err", 0}},
                                     {{"gen_ok", 1}, {"r_median", 0.96}, {"track_r_median", 0.95}, {"extrema_err", 2}},
                                     {{"gen_ok", 1}, {"r_median", 0.94}, {"track_r_median", kNaN}, {"extrema_err", 1}}};
    const Rows out = AggregateDraws(draws);
    auto get = [&](const std::string& m) {
        for (const auto& kv : out)
            if (kv.first == m) return kv.second;
        return -1e9;
    };
    CHECK(get("r_median") == doctest::Approx(0.94));
    CHECK(get("r_median_draw_spread") == doctest::Approx(0.06));
    CHECK(get("track_r_median") == doctest::Approx(0.875));  // the draw without a value is left out
    CHECK(get("track_r_median_draw_spread") == doctest::Approx(0.15));
    CHECK(get("extrema_err") == doctest::Approx(1));
    CHECK(get("extrema_err_draw_spread") == -1e9);  // only the scored rows get a spread
    CHECK(get("noise_draws") == 3);
    // One draw is the scene as it was: no extra rows.
    CHECK(AggregateDraws({draws[0]}).size() == draws[0].size());
}

TEST_CASE("bench: the CSV parser reads rows, nan and notes, and refuses a broken line") {
    std::string error;
    const auto t = ParseBenchCsv("# preset=app\r\nscene,metric,value\r\nclean,r_median,0.9990\r\nclean,amp_ratio,nan\r\n"
                                 "pan_x,r_median,0.98\r\n",
                                 &error);
    REQUIRE(t);
    CHECK(t->order == std::vector<std::string>{"clean", "pan_x"});
    CHECK(t->Get("clean", "r_median") == doctest::Approx(0.999));
    CHECK(std::isnan(t->Get("clean", "amp_ratio")));
    CHECK(std::isnan(t->Get("clean", "missing")));
    CHECK_FALSE(ParseBenchCsv("scene,metric,value\nbroken line\n", &error));
    CHECK_FALSE(ParseBenchCsv("scene,metric,value\n", &error));
}

TEST_CASE("bench: the acceptance rule passes an unchanged run without a gain") {
    const BenchTable a = Table("clean,gen_ok,1\nclean,r_median,0.99\nclean,r_spread,0\nclean,track_r_median,0.999\n"
                               "clean,peak_err_ms_median,10\nclean,pos_err_p95_px,2\nclean,amp_ratio,0.98\n"
                               "clean,ms_per_frame,20\nclean,rss_mb,200\n");
    const Comparison c = CompareBench(a, a, "main", "main");
    CHECK(c.quality_kept);
    CHECK(c.cost_kept);
    CHECK_FALSE(c.gain);
    CHECK(c.breaks.empty());
    CHECK(c.markdown.find("| clean |") != std::string::npos);
}

TEST_CASE("bench: the acceptance rule names every broken rule and scene") {
    const BenchTable a = Table("a,gen_ok,1\na,r_median,0.99\na,r_spread,0\na,track_r_median,0.99\na,pos_err_p95_px,10\n"
                               "a,amp_ratio,0.95\na,peak_err_ms_median,10\na,ms_per_frame,20\na,rss_mb,200\n"
                               "b,gen_ok,1\nb,r_median,0.90\nb,r_spread,0.05\nb,track_r_median,0.9\nb,pos_err_p95_px,0.5\n"
                               "b,amp_ratio,1.0\nb,peak_err_ms_median,10\nb,ms_per_frame,20\nb,rss_mb,200\n"
                               "c,gen_ok,1\nc,r_median,0.95\nc,ms_per_frame,20\nc,rss_mb,200\n"
                               "gone,gen_ok,1\ngone,r_median,0.9\n");
    const BenchTable b = Table("a,gen_ok,1\na,r_median,0.97\na,r_spread,0\na,track_r_median,0.97\na,pos_err_p95_px,11.5\n"
                               "a,amp_ratio,0.80\na,peak_err_ms_median,20\na,ms_per_frame,25\na,rss_mb,230\n"
                               "b,gen_ok,1\nb,r_median,0.86\nb,r_spread,0\nb,track_r_median,0.895\nb,pos_err_p95_px,1.4\n"
                               "b,amp_ratio,1.04\nb,peak_err_ms_median,20\nb,ms_per_frame,25\nb,rss_mb,200\n"
                               "c,gen_ok,0\nc,ms_per_frame,25\nc,rss_mb,200\n"
                               "new,gen_ok,1\nnew,r_median,0.9\n");
    const Comparison c = CompareBench(a, b, "before", "after");
    CHECK_FALSE(c.quality_kept);
    CHECK_FALSE(c.cost_kept);
    CHECK_FALSE(c.gain);
    auto has = [&](const std::string& needle) {
        for (const std::string& line : c.breaks)
            if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    CHECK(has("a: script r"));    // 0.02 down, spread 0: over 0.01
    CHECK(has("a: track r"));
    CHECK(has("a: pos p95"));     // +1.5 px: over 10 % and over 1 px
    CHECK(has("a: amp"));         // 0.05 -> 0.20 from 1
    CHECK(has("a: RSS"));         // +15 %
    CHECK_FALSE(has("b: script r"));  // 0.04 down, but the run spread is 0.05
    CHECK_FALSE(has("b: track r"));   // 0.005 down
    CHECK_FALSE(has("b: pos p95"));   // +0.9 px: under the 1 px floor
    CHECK_FALSE(has("b: amp"));
    CHECK(has("c: no script any more"));
    CHECK(has("ms/frame summed"));    // 60 -> 75: +25 %
    CHECK(c.markdown.find("gone (only before)") != std::string::npos);
    CHECK(c.markdown.find("new (only after)") != std::string::npos);
}

TEST_CASE("bench: the acceptance rule sees a gain in r or in peak error, and a noise spread widens rule 1") {
    const BenchTable a = Table("a,r_median,0.90\na,peak_err_ms_median,20\na,ms_per_frame,20\n"
                               "n,r_median,0.90\nn,r_median_draw_spread,0.06\nn,peak_err_ms_median,20\nn,ms_per_frame,20\n");
    const BenchTable better_r = Table("a,r_median,0.95\na,peak_err_ms_median,20\na,ms_per_frame,20\n"
                                      "n,r_median,0.85\nn,peak_err_ms_median,20\nn,ms_per_frame,20\n");
    const Comparison c1 = CompareBench(a, better_r, "a", "b");
    CHECK(c1.quality_kept);  // n fell by 0.05, inside its 0.06 draw spread
    CHECK(c1.gain);          // the median over the scenes rose
    const BenchTable better_peak = Table("a,r_median,0.90\na,peak_err_ms_median,17\na,ms_per_frame,20\n"
                                         "n,r_median,0.90\nn,peak_err_ms_median,19\nn,ms_per_frame,20\n");
    CHECK(CompareBench(a, better_peak, "a", "b").gain);  // median peak error 20 -> 18
    const BenchTable peak_1ms = Table("a,r_median,0.90\na,peak_err_ms_median,19\na,ms_per_frame,20\n"
                                      "n,r_median,0.90\nn,peak_err_ms_median,19\nn,ms_per_frame,20\n");
    CHECK_FALSE(CompareBench(a, peak_1ms, "a", "b").gain);
}

TEST_CASE("bench: a generated script is scored against a hand-written one") {
    // The reference strokes 1 Hz for 30 s; the generated script is the same stroke 120 ms late at 70 % swing.
    std::vector<Action> ref, gen, upside_down;
    for (int ms = 0; ms <= 30000; ms += 50) {
        const double v = std::sin(2 * kPi * ms / 1000.0);
        ref.push_back({ms, int(std::lround(50 + 45 * v))});
        gen.push_back({ms + 120, int(std::lround(50 + 31.5 * v))});
        upside_down.push_back({ms, int(std::lround(50 - 45 * v))});
    }
    const ReferenceScore s = ScoreAgainstReference(ref, gen);
    REQUIRE(s.scored);
    CHECK(s.r > 0.98);
    CHECK(std::abs(s.lag_ms) == doctest::Approx(120).epsilon(0.2));
    CHECK_FALSE(s.inverted);
    CHECK(s.windowed_r_median > 0.98);
    CHECK(s.peak_matched > 0.95);
    CHECK(s.peak_med <= 25);  // the constant offset is the lag, not a peak error
    CHECK(s.amp_ratio == doctest::Approx(0.7).epsilon(0.06));
    const ReferenceScore inv = ScoreAgainstReference(ref, upside_down);
    REQUIRE(inv.scored);
    CHECK(inv.inverted);
    CHECK(inv.peak_matched > 0.95);  // scored after turning it over, and flagged
    CHECK(inv.peak_med <= 25);
    // Nothing to compare: not scored, no numbers invented.
    const ReferenceScore none = ScoreAgainstReference(ref, {});
    CHECK_FALSE(none.scored);
    CHECK(std::isnan(none.r));
}
