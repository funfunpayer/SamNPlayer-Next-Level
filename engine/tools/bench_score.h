#pragma once

// The detection benchmark's scoring, presets and comparison (samn_bench),
// header-only so the engine tests can pin them without running the tool.
// Nothing here changes the engine: it only reads scripts and tracks.
//
// Synthetic truth: a scene's signal(t) is the script value it stands for
// (tests/engine/synthetic_scenes.h). A real clip's truth is a script written
// by hand (ScoreAgainstReference, through the comparison controller).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "samn/engine/compare.h"
#include "samn/engine/signal_chain.h"
#include "samn/funscript.h"
#include "samn/samn_file.h"

namespace samn::bench {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kWindowS = 10, kHopS = 5, kGridMs = 50;
constexpr double kMinSwing = 5;  // script units: a smaller wiggle is no extremum

// ---------------------------------------------------------------- maths

inline double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const double pos = p / 100.0 * (v.size() - 1);
    const size_t lo = size_t(std::floor(pos)), hi = size_t(std::ceil(pos));
    return v[lo] + (v[hi] - v[lo]) * (pos - lo);
}
inline double Median(const std::vector<double>& v) { return Percentile(v, 50); }

// Linear between actions; the value outside the script's span is not asked for.
inline double ScriptAt(const std::vector<samn::Action>& a, double t_ms) {
    if (a.empty()) return 0;
    if (t_ms <= a.front().at) return a.front().pos;
    if (t_ms >= a.back().at) return a.back().pos;
    const auto it = std::upper_bound(a.begin(), a.end(), t_ms,
                                     [](double t, const samn::Action& x) { return t < double(x.at); });
    const samn::Action& r = *it;
    const samn::Action& l = *(it - 1);
    return l.pos + (r.pos - l.pos) * (t_ms - l.at) / double(r.at - l.at);
}

inline double Pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = x.size();
    if (n < 3) return kNaN;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) mx += x[i], my += y[i];
    mx /= n;
    my /= n;
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    if (sxx < 1e-12) return kNaN;  // the truth does not move in this window
    if (syy < 1e-12) return 0;     // it does, the script does not
    return sxy / std::sqrt(sxx * syy);
}

// ---------------------------------------------------------------- truth

// What the scoring needs of a scene.
struct Truth {
    double fps = 25;
    int frames = 0;
    std::function<double(double)> signal;             // t in seconds -> script value (up = high)
    std::vector<std::pair<double, double>> hidden_s;  // [from, to) seconds with no tip to see

    bool Hidden(double t) const {
        for (const auto& h : hidden_s)
            if (t >= h.first && t < h.second) return true;
        return false;
    }
};

struct Key {
    double t_ms;
    int kind;  // +1 maximum of the signal, -1 minimum
    double value;
    bool visible;
};

// The true extrema on a 1 ms grid (the bench's first metric, kept as it was).
inline std::vector<Key> TruthKeys(const Truth& s) {
    std::vector<Key> keys;
    const double end_ms = (s.frames - 1) * 1000.0 / s.fps;
    std::vector<double> v;
    for (double t = 0; t <= end_ms; t += 1) v.push_back(s.signal(t / 1000.0));
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        const double t = double(i);
        if (t < 100 || t > end_ms - 100) continue;
        int kind = 0;
        if (v[i] > v[i - 1] && v[i] >= v[i + 1]) kind = 1;
        if (v[i] < v[i - 1] && v[i] <= v[i + 1]) kind = -1;
        if (kind) keys.push_back({t, kind, v[i], !s.Hidden(t / 1000.0)});
    }
    return keys;
}

// The same extrema with their time refined below the grid: the vertex of the
// parabola through the three grid values around each one. A stroke whose
// rate does not divide the frame rate has its peaks anywhere between two
// frames; this is the truth such a scene is scored against.
inline std::vector<Key> TruthKeysSubMs(const Truth& s) {
    std::vector<Key> keys = TruthKeys(s);
    for (Key& k : keys) {
        const double a = s.signal((k.t_ms - 1) / 1000.0), b = s.signal(k.t_ms / 1000.0),
                     c = s.signal((k.t_ms + 1) / 1000.0);
        const double den = a - 2 * b + c;
        if (std::abs(den) < 1e-12) continue;
        const double shift = std::clamp(0.5 * (a - c) / den, -1.0, 1.0);
        k.t_ms += shift;
        k.value = s.signal(k.t_ms / 1000.0);
    }
    return keys;
}

struct Extremum {
    double t_ms;
    int kind;
};

// Zigzag with a hysteresis of kMinSwing units. The first action and the last,
// still running, extreme are not counted (as the true keys leave the ends).
inline std::vector<Extremum> ScriptExtrema(const std::vector<samn::Action>& a) {
    std::vector<Extremum> out;
    if (a.size() < 3) return out;
    int dir = 0;
    size_t ext = 0;
    for (size_t i = 1; i < a.size(); ++i) {
        if (dir == 0) {
            if (a[i].pos - a[0].pos >= kMinSwing) dir = 1, ext = i;
            else if (a[0].pos - a[i].pos >= kMinSwing) dir = -1, ext = i;
        } else if (dir > 0) {
            if (a[i].pos > a[ext].pos) ext = i;
            else if (a[ext].pos - a[i].pos >= kMinSwing) out.push_back({double(a[ext].at), 1}), dir = -1, ext = i;
        } else {
            if (a[i].pos < a[ext].pos) ext = i;
            else if (a[i].pos - a[ext].pos >= kMinSwing) out.push_back({double(a[ext].at), -1}), dir = 1, ext = i;
        }
    }
    return out;
}

inline std::vector<double> WindowStarts(double total_s) {
    std::vector<double> starts;
    if (total_s <= kWindowS) return {0};
    for (double s = 0; s + kWindowS <= total_s + 1e-9; s += kHopS) starts.push_back(s);
    if (starts.back() + kWindowS < total_s - 1e-9) starts.push_back(total_s - kWindowS);
    return starts;
}

struct ScriptScore {
    double r_median = kNaN, r_min = kNaN;
    double peak_med = kNaN, peak_p95 = kNaN, peak_matched = kNaN;
    double extrema_err = kNaN, amp_ratio = kNaN;
    // Median of (script extremum - true key), ms: above 0 the script is late.
    double peak_bias = kNaN;
};

// The script against the truth. keys: TruthKeys for the first metric (whole
// milliseconds), TruthKeysSubMs for the sub-frame one.
inline ScriptScore Score(const std::vector<samn::Action>& a, const Truth& s, const std::vector<Key>& keys) {
    ScriptScore sc;
    if (a.size() < 2) return sc;
    const double total_s = s.frames / s.fps;
    std::vector<double> rs;
    for (const double w0 : WindowStarts(total_s)) {
        const double w1 = std::min(w0 + kWindowS, total_s);
        std::vector<double> x, y;
        for (double t = w0 * 1000; t < w1 * 1000; t += kGridMs) {
            if (t < a.front().at || t > a.back().at) continue;
            x.push_back(s.signal(t / 1000.0));
            y.push_back(ScriptAt(a, t));
        }
        const double r = Pearson(x, y);
        if (!std::isnan(r)) rs.push_back(r);
    }
    if (!rs.empty()) {
        sc.r_median = Median(rs);
        sc.r_min = *std::min_element(rs.begin(), rs.end());
    }
    if (keys.empty()) return sc;
    const auto ext = ScriptExtrema(a);
    sc.extrema_err = double(ext.size()) - double(keys.size());
    std::vector<double> errs, signed_errs;
    int visible = 0;
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!keys[k].visible) continue;
        ++visible;
        double gap = 500;
        if (k > 0) gap = std::min(gap, (keys[k].t_ms - keys[k - 1].t_ms) / 2);
        if (k + 1 < keys.size()) gap = std::min(gap, (keys[k + 1].t_ms - keys[k].t_ms) / 2);
        double best = 1e18, best_signed = 0;
        for (const Extremum& e : ext)
            if (e.kind == keys[k].kind && std::abs(e.t_ms - keys[k].t_ms) < best) {
                best = std::abs(e.t_ms - keys[k].t_ms);
                best_signed = e.t_ms - keys[k].t_ms;
            }
        if (best <= gap) errs.push_back(best), signed_errs.push_back(best_signed);
    }
    if (visible > 0) sc.peak_matched = double(errs.size()) / visible;
    if (!errs.empty()) {
        sc.peak_med = Median(errs);
        sc.peak_p95 = Percentile(errs, 95);
        sc.peak_bias = Median(signed_errs);
    }
    double lo = 1e18, hi = -1e18;
    for (double t = 0; t <= (s.frames - 1) * 1000.0 / s.fps; t += 5) {
        const double v = s.signal(t / 1000.0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    std::vector<double> ratios;
    if (hi - lo > 1e-9)
        for (size_t k = 0; k + 1 < keys.size(); ++k) {
            if (!keys[k].visible || !keys[k + 1].visible) continue;
            const double truth = 100.0 * std::abs(keys[k + 1].value - keys[k].value) / (hi - lo);
            const double got = std::abs(ScriptAt(a, keys[k + 1].t_ms) - ScriptAt(a, keys[k].t_ms));
            if (truth > 1e-9) ratios.push_back(got / truth);
        }
    if (!ratios.empty()) sc.amp_ratio = Median(ratios);
    return sc;
}

// The peak error no script can beat while its actions sit on analysed
// frames: the median distance from each visible true key to the nearest
// analysed sample time. At 25 fps and a 1 Hz stroke this is the 10 ms the
// first metric always showed; a lower analysis rate raises it.
inline double PeakFloorMs(const std::vector<Key>& keys, const std::vector<double>& sample_times_ms) {
    if (sample_times_ms.empty()) return kNaN;
    std::vector<double> d;
    for (const Key& k : keys) {
        if (!k.visible) continue;
        const auto it = std::lower_bound(sample_times_ms.begin(), sample_times_ms.end(), k.t_ms);
        double best = 1e18;
        if (it != sample_times_ms.end()) best = std::min(best, std::abs(*it - k.t_ms));
        if (it != sample_times_ms.begin()) best = std::min(best, std::abs(*(it - 1) - k.t_ms));
        d.push_back(best);
    }
    return Median(d);
}

// ---------------------------------------------------------------- real clips

struct ReferenceScore {
    bool scored = false;
    double r = kNaN, r_zero_lag = kNaN, windowed_r_median = kNaN;
    int lag_ms = 0, windows_scored = 0;
    bool inverted = false, boundary_pinned = false, low_confidence = false;
    // The reference's extrema against the generated script's, after the
    // generated script is moved by the best lag (and turned over when the
    // best orientation is inverted): what is left is the timing error of
    // single peaks, not a constant offset (that is lag_ms).
    double peak_med = kNaN, peak_p95 = kNaN, peak_matched = kNaN;
    // Median of the generated swing / the reference's swing at the
    // reference's consecutive extrema (both 0-100).
    double amp_ratio = kNaN;
};

// A generated script against a hand-written one for the same clip, with the
// comparison controller's rules (one timeline, a +-1000 ms lag window,
// windowed r) plus peak timing and amplitude.
inline ReferenceScore ScoreAgainstReference(const std::vector<samn::Action>& reference,
                                            const std::vector<samn::Action>& generated,
                                            const samn::engine::CompareSettings& settings = {}) {
    ReferenceScore out;
    const samn::engine::PairScore p = samn::engine::ScorePair(reference, generated, settings);
    if (!p.whole) return out;
    out.scored = true;
    out.r = p.whole->r;
    out.lag_ms = p.whole->lag_ms;
    out.inverted = p.whole->orientation == "inverted";
    out.low_confidence = p.whole->low_confidence;
    out.boundary_pinned = p.boundary_pinned;
    if (p.whole->r_zero_lag) out.r_zero_lag = *p.whole->r_zero_lag;
    if (p.windowed_r_median) out.windowed_r_median = *p.windowed_r_median;
    out.windows_scored = p.windows_scored;

    // The generated script on the reference's timeline. The sign of lag_ms
    // is settled here instead of assumed: the shift that fits better is used.
    const auto ref_ext = ScriptExtrema(reference);
    if (ref_ext.empty()) return out;
    auto moved = [&](int shift_ms) {
        std::vector<samn::Action> g = generated;
        for (samn::Action& a : g) {
            a.at += shift_ms;
            if (out.inverted) a.pos = 100 - a.pos;
        }
        return g;
    };
    auto errors = [&](const std::vector<samn::Action>& g, double* matched) {
        const auto ext = ScriptExtrema(g);
        std::vector<double> errs;
        for (size_t k = 0; k < ref_ext.size(); ++k) {
            double gap = 500;
            if (k > 0) gap = std::min(gap, (ref_ext[k].t_ms - ref_ext[k - 1].t_ms) / 2);
            if (k + 1 < ref_ext.size()) gap = std::min(gap, (ref_ext[k + 1].t_ms - ref_ext[k].t_ms) / 2);
            double best = 1e18;
            for (const Extremum& e : ext)
                if (e.kind == ref_ext[k].kind) best = std::min(best, std::abs(e.t_ms - ref_ext[k].t_ms));
            if (best <= gap) errs.push_back(best);
        }
        *matched = double(errs.size()) / ref_ext.size();
        return errs;
    };
    double m_plus = 0, m_minus = 0;
    const std::vector<samn::Action> g_plus = moved(out.lag_ms), g_minus = moved(-out.lag_ms);
    const std::vector<double> e_plus = errors(g_plus, &m_plus), e_minus = errors(g_minus, &m_minus);
    const bool plus = e_minus.empty() || (!e_plus.empty() && (m_plus > m_minus ||
                                                              (m_plus == m_minus && Median(e_plus) <= Median(e_minus))));
    const std::vector<double>& errs = plus ? e_plus : e_minus;
    const std::vector<samn::Action>& g = plus ? g_plus : g_minus;
    out.peak_matched = plus ? m_plus : m_minus;
    if (!errs.empty()) {
        out.peak_med = Median(errs);
        out.peak_p95 = Percentile(errs, 95);
    }
    std::vector<double> ratios;
    for (size_t k = 0; k + 1 < ref_ext.size(); ++k) {
        const double truth = std::abs(ScriptAt(reference, ref_ext[k + 1].t_ms) - ScriptAt(reference, ref_ext[k].t_ms));
        if (truth < kMinSwing) continue;
        if (ref_ext[k].t_ms < g.front().at || ref_ext[k + 1].t_ms > g.back().at) continue;
        ratios.push_back(std::abs(ScriptAt(g, ref_ext[k + 1].t_ms) - ScriptAt(g, ref_ext[k].t_ms)) / truth);
    }
    if (!ratios.empty()) out.amp_ratio = Median(ratios);
    return out;
}

// ---------------------------------------------------------------- presets

// What a bench run asks of Generate. "scene" as the axis means the scene's
// own (an X stroke asks for X, as the user would after seeing the preview).
struct BenchConfig {
    std::string preset = "default";
    samn::engine::SignalOptions signal;
    double max_analysis_fps = 0;
    int analysis_width = 0;
    bool camera_compensation = true, scene_cuts = true;
    // Not a switch of Generate (GenerateRequest has none): it reaches only
    // the extra track run, so the cut, loss, track r and position rows.
    bool adaptive_cuts = false;
    bool auto_ozone = false;
    bool contact_vibration = false;
    double contact_vibration_span = 0;
    std::string contact_vibration_curve;
    std::string axis = "scene";  // scene | auto | x | y
    std::vector<std::string> overrides;  // the --set arguments, as given
};

inline const std::vector<std::string>& PresetNames() {
    static const std::vector<std::string> names = {"default", "old", "app"};
    return names;
}

// default: the bench as it was (the Recommended profile, nothing else).
// old:     the app's Everyday Create switches with every frame at full
//          resolution: what Create did before the 15 fps / 640 px defaults.
// app:     what Everyday Create does now. The values are the controller's
//          defaults (app/src/generator_controller.h:465-478 and
//          GeneratorController::applySwitches, generator_controller.cpp:222-241);
//          tests/check_bench_preset.cmake fails when they drift apart.
inline bool ApplyPreset(const std::string& name, BenchConfig* c, std::string* error) {
    BenchConfig n;
    n.preset = name;
    n.signal.profile = samn::ProfileRecommended;
    if (name == "default") {
        *c = n;
        return true;
    }
    if (name != "old" && name != "app") {
        if (error) *error = "no preset named " + name + " (default, old, app)";
        return false;
    }
    n.signal.smooth_window = 11;          // generator_controller.h:470
    n.signal.min_peak_distance_ms = 150;  // generator_controller.h:471
    n.signal.peak_prominence = 0;         // generator_controller.h:472 (0 = the profile's 0.2)
    n.signal.max_gap_ms = 500;            // kGapBridgeMs, generator_controller.cpp:51 and :224
    n.signal.outlier_mad_factor = 5;      // kOutlierMadFactor, generator_controller.cpp:52 and :226
    n.signal.dynamic_range_ms = 3000;     // sliding dynamics, generator_controller.cpp:228
    n.signal.adaptive_error = 6;          // adaptive keyframes, generator_controller.cpp:230
    n.camera_compensation = true;         // generator_controller.h:466
    n.scene_cuts = true;                  // generator_controller.h:466
    n.auto_ozone = true;                  // generator_controller.h:466
    n.contact_vibration = true;           // generator_controller.h:487
    n.contact_vibration_span = 0.75;      // generator_controller.h:488
    n.contact_vibration_curve = "soft";   // generator_controller.h:489
    if (name == "app") {
        n.max_analysis_fps = 15;  // generator_controller.h:477 (skipped frames are grabbed, not read)
        n.analysis_width = 640;   // generator_controller.h:478
    }
    *c = n;
    return true;
}

inline const std::vector<std::string>& SetKeys() {
    static const std::vector<std::string> keys = {
        "profile",           "smooth_window",      "min_peak_distance_ms", "peak_prominence",
        "adaptive_error",    "dynamic_range_ms",   "max_gap_ms",           "outlier_mad_factor",
        "min_action_interval_ms", "norm_percentile", "detrend_window_ms",  "rdp_tolerance",
        "max_speed",         "max_analysis_fps",   "analysis_width",       "camera_compensation",
        "scene_cuts",        "adaptive_cuts",      "axis"};
    return keys;
}

// One "key=value". An unknown key, a missing value or a value that is not a
// number (or not one of the axis names) is an error: a typo must not run
// the bench with the old value under a new label.
inline bool ApplySet(const std::string& kv, BenchConfig* c, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const size_t eq = kv.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= kv.size()) return fail("--set needs key=value, got " + kv);
    const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
    if (key == "profile") {
        c->signal.profile = value;
        c->overrides.push_back(kv);
        return true;
    }
    if (key == "axis") {
        if (value != "scene" && value != "auto" && value != "x" && value != "y")
            return fail("axis is one of scene, auto, x, y; got " + value);
        c->axis = value;
        c->overrides.push_back(kv);
        return true;
    }
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(v)) return fail(key + " needs a number, got " + value);
    samn::engine::SignalOptions& s = c->signal;
    if (key == "smooth_window") s.smooth_window = int(v);
    else if (key == "min_peak_distance_ms") s.min_peak_distance_ms = int(v);
    else if (key == "peak_prominence") s.peak_prominence = v;
    else if (key == "adaptive_error") s.adaptive_error = v;
    else if (key == "dynamic_range_ms") s.dynamic_range_ms = v;
    else if (key == "max_gap_ms") s.max_gap_ms = v;
    else if (key == "outlier_mad_factor") s.outlier_mad_factor = v;
    else if (key == "min_action_interval_ms") s.min_action_interval_ms = v;
    else if (key == "norm_percentile") s.norm_percentile = v;
    else if (key == "detrend_window_ms") s.detrend_window_ms = v;
    else if (key == "rdp_tolerance") s.rdp_tolerance = v;
    else if (key == "max_speed") s.max_speed = v;
    else if (key == "max_analysis_fps") c->max_analysis_fps = v;
    else if (key == "analysis_width") c->analysis_width = int(v);
    else if (key == "camera_compensation") c->camera_compensation = v != 0;
    else if (key == "scene_cuts") c->scene_cuts = v != 0;
    else if (key == "adaptive_cuts") c->adaptive_cuts = v != 0;
    else {
        std::string known;
        for (const std::string& k : SetKeys()) known += (known.empty() ? "" : ", ") + k;
        return fail("unknown --set key " + key + " (known: " + known + ")");
    }
    c->overrides.push_back(kv);
    return true;
}

inline std::string ConfigNumber(double v) {
    std::ostringstream o;
    o << v;
    return o.str();
}

// Every value of the configuration as "key=value", in a fixed order (the
// run's header, --print-preset, and the drift check against the controller).
inline std::vector<std::pair<std::string, std::string>> ConfigLines(const BenchConfig& c) {
    const samn::engine::SignalOptions& s = c.signal;
    return {{"preset", c.preset},
            {"profile", s.profile},
            {"smooth_window", ConfigNumber(s.smooth_window)},
            {"min_peak_distance_ms", ConfigNumber(s.min_peak_distance_ms)},
            {"peak_prominence", ConfigNumber(s.peak_prominence)},
            {"adaptive_error", ConfigNumber(s.adaptive_error)},
            {"dynamic_range_ms", ConfigNumber(s.dynamic_range_ms)},
            {"max_gap_ms", ConfigNumber(s.max_gap_ms)},
            {"outlier_mad_factor", ConfigNumber(s.outlier_mad_factor)},
            {"min_action_interval_ms", ConfigNumber(s.min_action_interval_ms)},
            {"norm_percentile", ConfigNumber(s.norm_percentile)},
            {"detrend_window_ms", ConfigNumber(s.detrend_window_ms)},
            {"rdp_tolerance", ConfigNumber(s.rdp_tolerance)},
            {"max_speed", ConfigNumber(s.max_speed)},
            {"max_analysis_fps", ConfigNumber(c.max_analysis_fps)},
            {"analysis_width", ConfigNumber(c.analysis_width)},
            {"camera_compensation", c.camera_compensation ? "1" : "0"},
            {"scene_cuts", c.scene_cuts ? "1" : "0"},
            {"adaptive_cuts", c.adaptive_cuts ? "1" : "0"},
            {"auto_ozone", c.auto_ozone ? "1" : "0"},
            {"contact_vibration", c.contact_vibration ? "1" : "0"},
            {"contact_vibration_span", ConfigNumber(c.contact_vibration_span)},
            {"contact_vibration_curve", c.contact_vibration_curve},
            {"axis", c.axis}};
}

// ---------------------------------------------------------------- noise draws

using Rows = std::vector<std::pair<std::string, double>>;

// One scene measured on several noise draws: every metric becomes the median
// over the draws that have it (in the first draw's order), and the scored
// rows get their spread (max - min) as <metric>_draw_spread. Timing rows are
// not part of this: they are measured on the first draw only.
inline Rows AggregateDraws(const std::vector<Rows>& draws) {
    Rows out;
    if (draws.empty()) return out;
    if (draws.size() == 1) return draws.front();
    static const std::vector<std::string> spread_of = {"r_median", "track_r_median", "peak_err_ms_median",
                                                       "pos_err_p95_px", "amp_ratio"};
    for (const auto& first : draws.front()) {
        std::vector<double> vals;
        for (const Rows& d : draws)
            for (const auto& kv : d)
                if (kv.first == first.first && !std::isnan(kv.second)) vals.push_back(kv.second);
        out.push_back({first.first, vals.empty() ? kNaN : Median(vals)});
        if (std::find(spread_of.begin(), spread_of.end(), first.first) != spread_of.end() && !vals.empty())
            out.push_back({first.first + "_draw_spread",
                           *std::max_element(vals.begin(), vals.end()) - *std::min_element(vals.begin(), vals.end())});
    }
    out.push_back({"noise_draws", double(draws.size())});
    return out;
}

// ---------------------------------------------------------------- comparison

struct BenchTable {
    std::vector<std::string> order;                             // scenes, as measured
    std::map<std::string, std::map<std::string, double>> rows;  // scene -> metric -> value
    double Get(const std::string& scene, const std::string& metric) const {
        const auto s = rows.find(scene);
        if (s == rows.end()) return kNaN;
        const auto m = s->second.find(metric);
        return m == s->second.end() ? kNaN : m->second;
    }
};

// "scene,metric,value" with a header line; lines starting with # are notes.
inline std::optional<BenchTable> ParseBenchCsv(const std::string& text, std::string* error) {
    BenchTable t;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ++n;
        if (line.empty() || line[0] == '#' || line == "scene,metric,value") continue;
        const size_t a = line.find(','), b = line.rfind(',');
        if (a == std::string::npos || b == a) {
            if (error) *error = "line " + std::to_string(n) + " is not scene,metric,value";
            return std::nullopt;
        }
        const std::string scene = line.substr(0, a), metric = line.substr(a + 1, b - a - 1), value = line.substr(b + 1);
        if (!t.rows.count(scene)) t.order.push_back(scene);
        t.rows[scene][metric] = value == "nan" ? kNaN : std::atof(value.c_str());
    }
    if (t.order.empty()) {
        if (error) *error = "no rows";
        return std::nullopt;
    }
    return t;
}

inline std::optional<BenchTable> ReadBenchCsv(const std::filesystem::path& p, std::string* error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + p.u8string();
        return std::nullopt;
    }
    std::ostringstream s;
    s << in.rdbuf();
    return ParseBenchCsv(s.str(), error);
}

struct Comparison {
    std::string markdown;
    // Quality: rules 1, 4 and 5 (script r, track r, position error, amplitude,
    // and no scene that stopped generating). Gain: rule 2. Cost: rule 3.
    bool quality_kept = true, gain = false, cost_kept = true;
    std::vector<std::string> breaks;  // one line per broken rule and scene
    double median_r_before = kNaN, median_r_after = kNaN;
    double median_peak_before = kNaN, median_peak_after = kNaN;
    double ms_sum_before = 0, ms_sum_after = 0;
};

constexpr double kRuleRDrop = 0.01;       // rule 1 and the track r rule
constexpr double kRulePeakGainMs = 2;     // rule 2
constexpr double kRuleCostShare = 0.10;   // rule 3: ms/frame and RSS
constexpr double kRulePosShare = 0.10;    // rule 4: position error p95
constexpr double kRulePosFloorPx = 1.0;   // ... and by at least this much (the discs sit on whole pixels)
constexpr double kRuleAmpDrift = 0.05;    // rule 5: |amp_ratio - 1| may grow by this

// The acceptance rule applied to two CSVs of the same scenes ("before" is
// the baseline). Scenes that only one of them has are listed, not scored.
inline Comparison CompareBench(const BenchTable& before, const BenchTable& after, const std::string& before_name,
                               const std::string& after_name) {
    Comparison c;
    auto num = [](double v, int digits) {
        if (std::isnan(v)) return std::string("-");
        char b[48];
        std::snprintf(b, sizeof b, "%.*f", digits, v);
        return std::string(b);
    };
    auto spread = [](const BenchTable& t, const std::string& s, const char* run, const char* draw) {
        double v = 0;
        for (const char* m : {run, draw}) {
            const double x = t.Get(s, m);
            if (!std::isnan(x)) v = std::max(v, x);
        }
        return v;
    };
    std::ostringstream o;
    o << "| scene | script r | track r | peak err ms | pos p95 px | amp | ms/frame | RSS MB | broken |\n"
      << "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";
    std::vector<double> r_a, r_b, peak_a, peak_b;
    std::vector<std::string> only_before, only_after;
    for (const std::string& s : after.order)
        if (!before.rows.count(s)) only_after.push_back(s);
    for (const std::string& s : before.order) {
        if (!after.rows.count(s)) {
            only_before.push_back(s);
            continue;
        }
        std::vector<std::string> broken;
        auto pair = [&](const char* metric, int digits) {
            return num(before.Get(s, metric), digits) + " -> " + num(after.Get(s, metric), digits);
        };
        const double ok_a = before.Get(s, "gen_ok"), ok_b = after.Get(s, "gen_ok");
        if (ok_a == 1 && ok_b != 1) broken.push_back("no script any more");
        const double ra = before.Get(s, "r_median"), rb = after.Get(s, "r_median");
        if (!std::isnan(ra) && !std::isnan(rb)) {
            r_a.push_back(ra), r_b.push_back(rb);
            const double tol = std::max({kRuleRDrop, spread(before, s, "r_spread", "r_median_draw_spread"),
                                         spread(after, s, "r_spread", "r_median_draw_spread")});
            if (ra - rb > tol) broken.push_back("script r -" + num(ra - rb, 3) + " (allowed " + num(tol, 3) + ")");
        }
        const double ta = before.Get(s, "track_r_median"), tb = after.Get(s, "track_r_median");
        if (!std::isnan(ta) && !std::isnan(tb)) {
            const double tol = std::max({kRuleRDrop, spread(before, s, "", "track_r_median_draw_spread"),
                                         spread(after, s, "", "track_r_median_draw_spread")});
            if (ta - tb > tol) broken.push_back("track r -" + num(ta - tb, 3) + " (allowed " + num(tol, 3) + ")");
        }
        const double pa = before.Get(s, "peak_err_ms_median"), pb = after.Get(s, "peak_err_ms_median");
        if (!std::isnan(pa) && !std::isnan(pb)) peak_a.push_back(pa), peak_b.push_back(pb);
        const double ea = before.Get(s, "pos_err_p95_px"), eb = after.Get(s, "pos_err_p95_px");
        if (!std::isnan(ea) && !std::isnan(eb)) {
            const double tol = std::max({kRulePosShare * ea, kRulePosFloorPx,
                                         spread(before, s, "", "pos_err_p95_px_draw_spread"),
                                         spread(after, s, "", "pos_err_p95_px_draw_spread")});
            if (eb - ea > tol) broken.push_back("pos p95 +" + num(eb - ea, 1) + " px (allowed " + num(tol, 1) + ")");
        }
        const double aa = before.Get(s, "amp_ratio"), ab = after.Get(s, "amp_ratio");
        if (!std::isnan(aa) && !std::isnan(ab)) {
            const double tol = std::max({kRuleAmpDrift, spread(before, s, "", "amp_ratio_draw_spread"),
                                         spread(after, s, "", "amp_ratio_draw_spread")});
            const double worse = std::abs(ab - 1) - std::abs(aa - 1);
            if (worse > tol) broken.push_back("amp " + num(aa, 2) + " -> " + num(ab, 2));
        }
        const double ma = before.Get(s, "ms_per_frame"), mb = after.Get(s, "ms_per_frame");
        if (!std::isnan(ma) && !std::isnan(mb)) c.ms_sum_before += ma, c.ms_sum_after += mb;
        const double ga = before.Get(s, "rss_mb"), gb = after.Get(s, "rss_mb");
        if (!std::isnan(ga) && !std::isnan(gb) && ga > 0 && gb > ga * (1 + kRuleCostShare)) {
            c.cost_kept = false;
            c.breaks.push_back(s + ": RSS " + num(ga, 0) + " -> " + num(gb, 0) + " MB");
        }
        std::string all;
        for (const std::string& b : broken) {
            all += (all.empty() ? "" : "; ") + b;
            c.breaks.push_back(s + ": " + b);
            c.quality_kept = false;
        }
        o << "| " << s << " | " << pair("r_median", 3) << " | " << pair("track_r_median", 3) << " | "
          << pair("peak_err_ms_median", 1) << " | " << pair("pos_err_p95_px", 1) << " | " << pair("amp_ratio", 2)
          << " | " << pair("ms_per_frame", 2) << " | " << pair("rss_mb", 0) << " | " << (all.empty() ? "" : "**" + all + "**")
          << " |\n";
    }
    c.median_r_before = Median(r_a), c.median_r_after = Median(r_b);
    c.median_peak_before = Median(peak_a), c.median_peak_after = Median(peak_b);
    c.gain = (!std::isnan(c.median_r_after) && c.median_r_after > c.median_r_before + 1e-9) ||
             (!std::isnan(c.median_peak_after) && c.median_peak_before - c.median_peak_after >= kRulePeakGainMs);
    if (c.ms_sum_before > 0 && c.ms_sum_after > c.ms_sum_before * (1 + kRuleCostShare)) {
        c.cost_kept = false;
        c.breaks.push_back("ms/frame summed " + num(c.ms_sum_before, 1) + " -> " + num(c.ms_sum_after, 1));
    }
    std::ostringstream m;
    m << "Before: " << before_name << ". After: " << after_name << ". Each cell is before -> after.\n\n" << o.str() << "\n";
    if (!only_before.empty() || !only_after.empty()) {
        m << "Not compared:";
        for (const std::string& s : only_before) m << " " << s << " (only before)";
        for (const std::string& s : only_after) m << " " << s << " (only after)";
        m << ".\n\n";
    }
    const double share = c.ms_sum_before > 0 ? 100.0 * (c.ms_sum_after / c.ms_sum_before - 1) : kNaN;
    m << "| rule | result |\n|---|---|\n"
      << "| 1. no scene's script r drops by more than max(0.01, its spread) | "
      << (c.quality_kept ? "holds" : "**broken** (the scenes are marked above)") << " |\n"
      << "| 2. median script r up, or median peak error down by 2 ms or more | median r "
      << num(c.median_r_before, 4) << " -> " << num(c.median_r_after, 4) << ", median peak error "
      << num(c.median_peak_before, 1) << " -> " << num(c.median_peak_after, 1) << " ms: "
      << (c.gain ? "met" : "**not met**") << " |\n"
      << "| 3. ms/frame (summed over the scenes) and RSS up by at most 10 % | " << num(c.ms_sum_before, 1) << " -> "
      << num(c.ms_sum_after, 1) << " ms (" << (share >= 0 ? "+" : "") << num(share, 1) << " %): "
      << (c.cost_kept ? "holds" : "**broken**") << " |\n"
      << "| 4. track r down by at most max(0.01, spread); position error p95 up by at most max(10 %, 1 px, spread) | see the column |\n"
      << "| 5. amplitude ratio no further from 1 than before, by more than max(0.05, spread) | see the column |\n\n"
      << "Verdict: quality " << (c.quality_kept ? "kept" : "**not kept**") << ", gain "
      << (c.gain ? "yes" : "no") << ", cost " << (c.cost_kept ? "kept" : "**not kept**") << ".\n";
    c.markdown = m.str();
    return c;
}

}  // namespace samn::bench
