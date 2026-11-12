// samn_bench: the detection benchmark. Runs the real Generate pipeline (the
// Recommended profile, no tip hints) on fixed-seed synthetic scenes with
// known ground truth and scores the script and the cost. Numbers only; every
// scene is a moving disc or a test pattern, never real media.
//
//   samn_bench [--scenes a,b,...] [--out <dir>] [--runs N] [--quick] [--list]
//
// Writes samn_bench.csv (scene, metric, value) and samn_bench.md into the
// folder and prints the table. Not run by ctest, except the one-scene smoke
// check (`--quick --scenes clean --require-r 0.9`).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/engine/generate.h"
#include "samn/engine/post_track.h"
#include "samn/engine/track_box.h"
#include "samn/fileio.h"
#include "samn/samn_file.h"
#include "synthetic_scenes.h"

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace samn::engine;
namespace fs = std::filesystem;
namespace tc = samn::engine::testclips;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kWindowS = 10, kHopS = 5, kGridMs = 50;
constexpr double kMinSwing = 5;     // script units: a smaller wiggle is no extremum
constexpr double kJumpPx = 60;      // a frame-to-frame box move above this is a jump
constexpr int kLongSceneRuns = 1;   // a 300 s scene is measured once

const char* kHelp = R"(samn_bench: the detection benchmark (synthetic scenes, the real Generate pipeline)

usage: samn_bench [--scenes a,b,...] [--out <dir>] [--runs N] [--warmups N]
                  [--quick] [--no-track] [--timing-only] [--keep-clips] [--require-r X] [--list] [--help]

  --scenes   comma list of scene names (default: all; --list shows them)
  --out      output folder (default ./samn_bench_out): samn_bench.csv, samn_bench.md,
             and clips/<scene>_truth.csv (ground-truth key times)
  --runs     timed runs per scene (default 5; the median is reported)
  --warmups  untimed runs before them (default 3)
  --quick    1 run, no warm-up, the 300 s scene left out unless named (CI smoke)
  --no-track     skip the extra, untimed TrackBox run (cut, loss and jump metrics)
  --timing-only  only the timed Generate runs: ms_per_frame, ms_spread, rss_mb
  --keep-clips   keep the generated clips and scripts
  --require-r X  exit 3 when a scene's median windowed r is below X

Metrics (CSV: one row per scene x metric):
  r_median, r_min    10 s windows (5 s hop) of Pearson r, script against truth
  track_r_median, track_r_min   the same r on the raw box-centre track, before the signal
                     chain (its detrend hides a slow camera error; needs the extra track run)
  r_spread           max - min of r_median over the timed runs
  peak_err_ms_*      median / p95 of |script extremum - true key time| (visible keys)
  peak_matched       share of visible true keys with a script extremum of the same kind
  extrema_err        script extrema minus true extrema (a swing under 5 units is no extremum)
  amp_ratio          median swing of the script at true key pairs / the true swing as 0-100
  cuts_found, false_cuts   the engine's own scene cuts; false = not within 2 frames of a true cut
  lost_share         share of frames the tracker flags lost
  camera_gated_share share of frames whose 1 px camera fit was not trusted (inliers < 0.5 or implausible)
  camera_unmeasured_share  share of frames without a trusted camera step (gated or no estimate: they add nothing)
  loss_precision, loss_recall   lost flags against the true occlusion mask (occlusion scenes)
  pos_err_p95_px     p95 over frames of |track motion since the first frame - true motion of the disc in the
                     scene| on the chosen axis, px (camera removed by the truth; needs the extra track run)
  jumps_over_60px    frame-to-frame box moves above 60 px (the lighting-cut scene has one true)
  ms_per_frame, ms_spread   Generate wall time per frame: median and max - min of the runs
  rss_mb             peak resident memory of the scene's own process (Linux; 0 elsewhere)

Acceptance rule for a change (compare its CSV with the baseline's):
  1. no scene's r_median drops by more than max(0.01, its r_spread);
  2. median r rises, or median peak error falls by >= 2 ms (over the scenes);
  3. ms_per_frame rises by <= 10 % and rss_mb by <= 10 %;
  4. before a change becomes a default: the Owner gate through samn_compare on the
     private clips (numbers only; no media in any repository).
)";

struct Row {
    std::string scene, metric;
    double value;
};
using Rows = std::vector<std::pair<std::string, double>>;

// ---------------------------------------------------------------- scenes

struct SceneDef {
    std::string name, what;
    bool long_scene = false;
    cv::Rect box{100, 40, 120, 160};
    TrackAxis axis = TrackAxis::kAuto;
    bool expect_no_motion = false;
    std::function<tc::SceneInfo(const fs::path&)> make;
};

tc::SceneInfo Wrap(bool ok, int frames, double amp, tc::SceneInfo s = {}) {
    s.ok = ok;
    s.frames = frames;
    s.signal = [amp](double t) { return -tc::Stroke(t, amp, 1.0); };
    return s;
}

std::vector<SceneDef> Scenes() {
    const cv::Rect kBoxX(100, 60, 120, 120);
    std::vector<SceneDef> v;
    auto add = [&](SceneDef d) { v.push_back(std::move(d)); };
    static constexpr int kSec = 12;
    static constexpr int kFr = int(tc::kFps * kSec);
    add({"clean", "1 Hz disc, amp 80, textured background", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return Wrap(tc::WriteMoving(p, kSec, 80, 1.0), kFr, 80); }});
    add({"pan_y", "camera pan, Y stroke (WritePanning)", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return Wrap(tc::WritePanning(p, kSec, 80), kFr, 80); }});
    add({"pan_x", "camera pan, X stroke, axis X", false, kBoxX, TrackAxis::kX, false,
         [](const fs::path& p) { return tc::WritePanningX(p, kSec, 80); }});
    add({"zoom", "camera zoom 1.0 -> 1.4, Y stroke", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteZoomRamp(p, kSec, 80, 1.4); }});
    // Faster than the 3 s default detrend window removes (0.25 Hz), and just
    // below the speed (about 3 px per frame) at which the fixed scene-cut
    // test starts to fire on this texture.
    add({"pan_x_fast", "camera pan x +-45 px at 0.25 Hz, X stroke, axis X", false, kBoxX, TrackAxis::kX, false,
         [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(45, 0.25);
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"pan_zoom", "pan x +-40 px at 0.2 Hz, zoom 1 +- 0.25 at 0.15 Hz, Y stroke", false, {100, 40, 120, 160},
         TrackAxis::kAuto, false, [](const fs::path& p) {
             tc::CameraScene c;
             c.camera.pan_x = tc::Sine(40, 0.2);
             c.camera.zoom = [](double t) { return 1 + 0.25 * std::sin(2 * tc::kPi * 0.15 * t); };
             return tc::WriteCameraScene(p, kSec, c);
         }});
    // Noisy, small and busy pictures (PR #201 review): the 1 px inlier share
    // of a noisy background falls under the gate in many frames.
    add({"pan_noisy", "pan_x_fast on a sparse texture with blur 5, noise sigma 25, JPEG 20", false, kBoxX,
         TrackAxis::kX, false, [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(45, 0.25);
             c.texture_density = 0.6;
             c.blur_px = 5;
             c.noise_sigma = 25;
             c.jpeg_quality = 20;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"pan_lowres_noisy", "192x144, pan x +-27 px at 0.25 Hz, blur 3, noise sigma 20, JPEG 20, X stroke", false,
         {60, 36, 72, 72}, TrackAxis::kX, false, [](const fs::path& p) {
             tc::CameraScene c;
             c.w = 192;
             c.h = 144;
             c.disc_r = 21;
             c.amp = 48;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(27, 0.25);
             c.texture_density = 0.8;
             c.blur_px = 3;
             c.noise_sigma = 20;
             c.jpeg_quality = 20;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"pan_fg", "pan x +-40 px at 0.3 Hz under a large moving textured disc, X stroke", false, kBoxX, TrackAxis::kX,
         false, [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(40, 0.3);
             c.foreground = true;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    // Camera motion the gate must not turn into a drift (second review of
    // the camera fixes): shake that does not persist, parallax (a near layer
    // moving 1.6x the far one), and a camera that stops abruptly. Seeds and
    // sizes picked once, not tuned.
    add({"shake", "hand shake +-6 / +-4 px at 4-9 Hz, X stroke", false, kBoxX, TrackAxis::kX, false,
         [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Shake(6, 7);
             c.camera.pan_y = tc::Shake(4, 8);
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"shake_noisy", "shake on a sparse texture with blur 5, noise sigma 25, JPEG 20", false, kBoxX, TrackAxis::kX,
         false, [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Shake(6, 7);
             c.camera.pan_y = tc::Shake(4, 8);
             c.texture_density = 0.6;
             c.blur_px = 5;
             c.noise_sigma = 25;
             c.jpeg_quality = 20;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"parallax", "pan x +-45 px at 0.25 Hz, a near layer moving 1.6x, X stroke", false, kBoxX, TrackAxis::kX, false,
         [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(45, 0.25);
             c.near_factor = 1.6;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"parallax_noisy", "parallax with blur 3, noise sigma 20, JPEG 20, sparser texture", false, kBoxX, TrackAxis::kX,
         false, [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(45, 0.25);
             c.near_factor = 1.6;
             c.texture_density = 0.8;
             c.blur_px = 3;
             c.noise_sigma = 20;
             c.jpeg_quality = 20;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"stop_noisy", "pan x at 40 px/s for 1.5 s, stop 1.5 s, repeated; blur 5, noise 25, JPEG 20", false, kBoxX,
         TrackAxis::kX, false, [](const fs::path& p) {
             tc::CameraScene c;
             c.stroke_x = true;
             const auto go = tc::StopGo(40, 1.5, 1.5);
             c.camera.pan_x = [go](double t) { return go(t) - 120; };
             c.texture_density = 0.6;
             c.blur_px = 5;
             c.noise_sigma = 25;
             c.jpeg_quality = 20;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    add({"pan_lowres_fg", "192x144 pan under the moving disc, sparse texture, blur 5, noise sigma 4, JPEG 30", false,
         {60, 36, 72, 72}, TrackAxis::kX, false, [](const fs::path& p) {
             tc::CameraScene c;
             c.w = 192;
             c.h = 144;
             c.disc_r = 21;
             c.amp = 48;
             c.stroke_x = true;
             c.camera.pan_x = tc::Sine(27, 0.25);
             c.foreground = true;
             c.texture_density = 0.6;
             c.blur_px = 5;
             c.noise_sigma = 4;
             c.jpeg_quality = 30;
             return tc::WriteCameraScene(p, kSec, c);
         }});
    for (const double len : {0.5, 2.0, 4.0}) {
        char name[32];
        std::snprintf(name, sizeof name, "occl_%gs", len);
        add({name, "disc hidden for " + std::to_string(len).substr(0, 3) + " s from 6 s", false, {100, 40, 120, 160},
             TrackAxis::kAuto, false,
             [len](const fs::path& p) { return tc::WriteOcclusion(p, 16, 80, 6.0, 6.0 + len); }});
    }
    add({"blur", "2 Hz disc, amp 110, full-frame motion blur", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteMotionBlur(p, kSec, 110, 2.0); }});
    add({"noise_jpeg", "noise sigma 8, JPEG quality 20, then mp4v", false, {100, 40, 120, 160}, TrackAxis::kAuto,
         false, [](const fs::path& p) { return tc::WriteNoisyCompressed(p, kSec, 80, 8.0, 20); }});
    add({"drop30", "30 % of the frames repeated (dropped)", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteFrozenFrames(p, kSec, 80, 0.30, 4); }});
    add({"distract_anti", "second disc, anti-phase", false, {55, 40, 110, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteDistractor(p, kSec, 80, tc::kPi); }});
    add({"distract_quarter", "second disc, quarter phase", false, {55, 40, 110, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteDistractor(p, kSec, 80, tc::kPi / 2); }});
    add({"hard_cut", "hard cut to another background at 6 s", false, {100, 40, 120, 160}, TrackAxis::kAuto, false,
         [](const fs::path& p) {
             tc::SceneInfo s = Wrap(tc::WriteHardCut(p, kSec, 150, 80), kFr, 80);
             s.cut_frames = {150};
             return s;
         }});
    add({"light_cut_jump", "lighting cut at 6 s, the disc 120 px further right", false, {100, 40, 120, 160},
         TrackAxis::kAuto, false, [](const fs::path& p) {
             tc::SceneInfo s = Wrap(tc::WriteLightingCutJump(p, kSec, 150), kFr, 20);
             s.cut_frames = {150};
             return s;
         }});
    add({"fast_small", "256x144, 30 fps, 2.5 Hz, no cut", false, {78, 10, 100, 124}, TrackAxis::kAuto, false,
         [](const fs::path& p) { return tc::WriteFastSmall(p, kSec, 70, 2.5); }});
    add({"still", "a still disc: the engine must refuse (no motion)", false, {100, 40, 120, 160}, TrackAxis::kAuto,
         true, [](const fs::path& p) {
             tc::SceneInfo s = Wrap(tc::WriteMoving(p, 6, 0, 1.0), int(tc::kFps * 6), 0);
             s.signal = [](double) { return 0.0; };
             return s;
         }});
    add({"drift_300s", "300 s, 1 Hz stroke on a +-25 px, 90 s drift", true, {100, 40, 120, 160}, TrackAxis::kAuto,
         false, [](const fs::path& p) { return tc::WriteDrift(p, 300, 60, 25, 90); }});
    return v;
}

// ---------------------------------------------------------------- maths

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const double pos = p / 100.0 * (v.size() - 1);
    const size_t lo = size_t(std::floor(pos)), hi = size_t(std::ceil(pos));
    return v[lo] + (v[hi] - v[lo]) * (pos - lo);
}
double Median(const std::vector<double>& v) { return Percentile(v, 50); }

// Linear between actions; the value outside the script's span is not asked for.
double ScriptAt(const std::vector<samn::Action>& a, double t_ms) {
    if (a.empty()) return 0;
    if (t_ms <= a.front().at) return a.front().pos;
    if (t_ms >= a.back().at) return a.back().pos;
    const auto it = std::upper_bound(a.begin(), a.end(), t_ms,
                                     [](double t, const samn::Action& x) { return t < double(x.at); });
    const samn::Action& r = *it;
    const samn::Action& l = *(it - 1);
    return l.pos + (r.pos - l.pos) * (t_ms - l.at) / double(r.at - l.at);
}

double Pearson(const std::vector<double>& x, const std::vector<double>& y) {
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

struct Key {
    double t_ms;
    int kind;  // +1 maximum of the signal, -1 minimum
    double value;
    bool visible;
};

std::vector<Key> TruthKeys(const tc::SceneInfo& s) {
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

struct Extremum {
    double t_ms;
    int kind;
};

// Zigzag with a hysteresis of kMinSwing units. The first action and the last,
// still running, extreme are not counted (as the true keys leave the ends).
std::vector<Extremum> ScriptExtrema(const std::vector<samn::Action>& a) {
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

std::vector<double> WindowStarts(double total_s) {
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
};

ScriptScore Score(const std::vector<samn::Action>& a, const tc::SceneInfo& s, const std::vector<Key>& keys) {
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
    std::vector<double> errs;
    int visible = 0;
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!keys[k].visible) continue;
        ++visible;
        double gap = 500;
        if (k > 0) gap = std::min(gap, (keys[k].t_ms - keys[k - 1].t_ms) / 2);
        if (k + 1 < keys.size()) gap = std::min(gap, (keys[k + 1].t_ms - keys[k].t_ms) / 2);
        double best = 1e18;
        for (const Extremum& e : ext)
            if (e.kind == keys[k].kind) best = std::min(best, std::abs(e.t_ms - keys[k].t_ms));
        if (best <= gap) errs.push_back(best);
    }
    if (visible > 0) sc.peak_matched = double(errs.size()) / visible;
    if (!errs.empty()) {
        sc.peak_med = Median(errs);
        sc.peak_p95 = Percentile(errs, 95);
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

// ---------------------------------------------------------------- one scene

std::vector<samn::Action> ReadActions(const fs::path& p) {
    std::ifstream in(p);
    const nlohmann::json j = nlohmann::json::parse(in);
    std::vector<samn::Action> out;
    for (const auto& a : j["actions"]) out.push_back({a["at"].get<int64_t>(), a["pos"].get<int>()});
    return out;
}

struct Options {
    fs::path out = "samn_bench_out";
    std::vector<std::string> scenes;
    int runs = 5, warmups = 3;
    bool quick = false, keep = false, list = false, no_track = false, timing_only = false;
    double require_r = -1;
};

struct RunOutcome {
    bool ok = false;
    std::string error;
    double ms = 0;
};

RunOutcome RunGenerate(const GenerateRequest& req) {
    RunOutcome o;
    GenerateEventKind last = GenerateEventKind::kProgress;
    std::string message;
    const auto t0 = std::chrono::steady_clock::now();
    Generate(req, 1, {}, [&](const GenerateEvent& e) {
        if (e.kind == GenerateEventKind::kDone || e.kind == GenerateEventKind::kError ||
            e.kind == GenerateEventKind::kCanceled) {
            last = e.kind;
            message = e.message;
        }
    });
    o.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    o.ok = last == GenerateEventKind::kDone;
    if (!o.ok) o.error = message;
    return o;
}

void WriteTruthCsv(const fs::path& p, const tc::SceneInfo& s, const std::vector<Key>& keys) {
    std::ostringstream o;
    o << "time_ms,kind,value,visible\n";
    for (const Key& k : keys)
        o << k.t_ms << "," << (k.kind > 0 ? "max" : "min") << "," << k.value << "," << (k.visible ? 1 : 0) << "\n";
    for (const auto& h : s.hidden_s) {
        o << h.first * 1000 << ",hidden_from," << 0 << ",0\n";
        o << h.second * 1000 << ",hidden_to," << 0 << ",0\n";
    }
    for (int c : s.cut_frames) o << c * 1000.0 / s.fps << ",cut," << c << ",1\n";
    samn::WriteFileAtomic(p, o.str());
}

// Runs one scene in this process and returns its metric rows.
Rows MeasureScene(const SceneDef& d, const Options& opt, std::string* error) {
    Rows rows;
    const fs::path dir = opt.out / "clips";
    fs::create_directories(dir);
    const fs::path clip = dir / (d.name + ".mp4");
    const tc::SceneInfo info = d.make(clip);
    if (!info.ok) {
        *error = "cannot write the clip (video writer)";
        return rows;
    }
    const std::vector<Key> keys = d.expect_no_motion || opt.timing_only ? std::vector<Key>{} : TruthKeys(info);
    if (!opt.timing_only) WriteTruthCsv(dir / (d.name + "_truth.csv"), info, keys);

    GenerateRequest req;
    req.video = clip;
    req.box = d.box;
    req.axis = d.axis;
    req.signal.profile = samn::ProfileRecommended;
    req.output = dir / (d.name + ".funscript");
    req.auto_ozone = false;
    req.overwrite = true;

    const int warmups = d.long_scene ? 0 : opt.warmups;
    const int runs = d.long_scene ? std::min(opt.runs, kLongSceneRuns) : opt.runs;
    for (int i = 0; i < warmups; ++i) RunGenerate(req);
    std::vector<double> ms, rmed;
    ScriptScore score;
    bool ok = false;
    std::string gen_error;
    for (int i = 0; i < runs; ++i) {
        const RunOutcome o = RunGenerate(req);
        ok = o.ok;
        gen_error = o.error;
        ms.push_back(o.ms / info.frames);
        if (!o.ok || opt.timing_only) continue;
        score = Score(ReadActions(req.output), info, keys);
        rmed.push_back(score.r_median);
    }
    rows.push_back({"frames", double(info.frames)});
    rows.push_back({"gen_ok", ok ? 1.0 : 0.0});
    if (!ok) {
        const bool no_motion = gen_error.find("no discernible motion") != std::string::npos;
        rows.push_back({"no_motion_error", no_motion ? 1.0 : 0.0});
        if (!(d.expect_no_motion && no_motion)) *error = gen_error;
    }
    if (ok && !opt.timing_only) {
        rows.push_back({"r_median", score.r_median});
        rows.push_back({"r_min", score.r_min});
        double lo = 1e18, hi = -1e18;
        for (double r : rmed)
            if (!std::isnan(r)) lo = std::min(lo, r), hi = std::max(hi, r);
        rows.push_back({"r_spread", hi >= lo ? hi - lo : kNaN});
        rows.push_back({"peak_err_ms_median", score.peak_med});
        rows.push_back({"peak_err_ms_p95", score.peak_p95});
        rows.push_back({"peak_matched", score.peak_matched});
        rows.push_back({"extrema_err", score.extrema_err});
        rows.push_back({"amp_ratio", score.amp_ratio});
    }

    // The engine's own track: scene cuts, lost flags, jumps (one more, untimed run).
    TrackOptions to;
    to.axis = d.axis;
    std::string track_error;
    const bool track = !opt.no_track && !opt.timing_only;
    const auto tr = track ? TrackBox(clip, d.box, to, &track_error) : std::nullopt;
    if (tr) {
        int found = 0, false_cuts = 0;
        for (int c : tr->scene_cuts) {
            ++found;
            bool matched = false;
            for (int t : info.cut_frames) matched = matched || std::abs(c - t) <= 2;
            if (!matched) ++false_cuts;
        }
        rows.push_back({"cuts_found", double(found)});
        rows.push_back({"false_cuts", double(false_cuts)});
        const size_t n = tr->lost.size();
        size_t flagged = 0, masked = 0, both = 0;
        for (size_t i = 0; i < n; ++i) {
            const bool lost = tr->lost[i] != 0, hidden = info.Hidden(tr->times_ms[i] / 1000.0);
            flagged += lost, masked += hidden, both += lost && hidden;
        }
        rows.push_back({"lost_share", n ? double(flagged) / n : kNaN});
        // Camera: frames whose 1 px fit was not trusted, and all frames without
        // a trusted step (also those without any estimate).
        const double frames = std::max(1, tr->stats.total_frames);
        rows.push_back({"camera_gated_share", tr->stats.camera_frames_gated / frames});
        rows.push_back({"camera_unmeasured_share", tr->stats.camera_frames_lost / frames});
        if (masked > 0) {
            rows.push_back({"loss_precision", flagged ? double(both) / flagged : kNaN});
            rows.push_back({"loss_recall", double(both) / masked});
        }
        // The raw box-centre track against the truth, before the signal chain
        // (its detrend hides a slow camera error that this shows).
        std::vector<double> tr_r;
        const double total_s = info.frames / info.fps;
        for (const double w0 : WindowStarts(total_s)) {
            std::vector<double> x, y;
            for (size_t i = 0; i < tr->times_ms.size() && i < tr->positions.size(); ++i) {
                const double t = tr->times_ms[i] / 1000.0;
                if (t < w0 || t >= std::min(w0 + kWindowS, total_s)) continue;
                x.push_back(info.signal(t));
                y.push_back(-tr->positions[i]);
            }
            const double r = Pearson(x, y);
            if (!std::isnan(r)) tr_r.push_back(r);
        }
        if (!tr_r.empty()) {
            rows.push_back({"track_r_median", Median(tr_r)});
            rows.push_back({"track_r_min", *std::min_element(tr_r.begin(), tr_r.end())});
        }
        // Position error: the track's motion since its first frame against
        // the true motion of the disc in the scene (the camera removed), p95.
        std::vector<double> pos_err;
        for (size_t i = 0; i < tr->times_ms.size() && i < tr->positions.size(); ++i) {
            const double truth = -info.signal(tr->times_ms[i] / 1000.0) + info.signal(tr->times_ms[0] / 1000.0);
            pos_err.push_back(std::abs((tr->positions[i] - tr->positions[0]) - truth));
        }
        if (!pos_err.empty() && !tr->distance_stroke) rows.push_back({"pos_err_p95_px", Percentile(pos_err, 95)});
        int jumps = 0;
        for (size_t i = 1; i < tr->xs.size() && i < tr->ys.size(); ++i)
            if (std::hypot(tr->xs[i] - tr->xs[i - 1], tr->ys[i] - tr->ys[i - 1]) > kJumpPx) ++jumps;
        rows.push_back({"jumps_over_60px", double(jumps)});
    } else if (track) {
        *error = "TrackBox: " + track_error;
    }
    rows.push_back({"ms_per_frame", Median(ms)});
    rows.push_back({"ms_spread", ms.empty() ? kNaN : *std::max_element(ms.begin(), ms.end()) - *std::min_element(ms.begin(), ms.end())});

    if (!opt.keep) {
        std::error_code ec;
        for (const char* ext : {".mp4", ".funscript", ".samn"}) fs::remove(dir / (d.name + ext), ec);
    }
    return rows;
}

// ---------------------------------------------------------------- output

std::string Num(double v, int digits) {
    if (std::isnan(v)) return "nan";
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}

int Digits(const std::string& metric) {
    static const std::map<std::string, int> d = {{"frames", 0},      {"gen_ok", 0},        {"no_motion_error", 0},
                                                 {"extrema_err", 0}, {"cuts_found", 0},    {"false_cuts", 0},
                                                 {"jumps_over_60px", 0}, {"peak_err_ms_median", 1},
                                                 {"peak_err_ms_p95", 1}, {"ms_per_frame", 2}, {"ms_spread", 2},
                                                 {"rss_mb", 1}, {"pos_err_p95_px", 2}};
    const auto it = d.find(metric);
    return it == d.end() ? 4 : it->second;
}

std::string Markdown(const std::vector<std::string>& order, const std::map<std::string, std::map<std::string, double>>& m,
                     const std::map<std::string, std::string>& what, const Options& opt) {
    struct Col {
        const char* metric;
        const char* head;
        int digits;
    };
    const Col cols[] = {{"r_median", "r med", 3},      {"r_min", "r min", 3},         {"r_spread", "r spread", 3}, {"track_r_median", "track r", 3},
                        {"peak_err_ms_median", "peak med ms", 1}, {"peak_err_ms_p95", "peak p95 ms", 1},
                        {"extrema_err", "ext err", 0}, {"amp_ratio", "amp", 2},       {"false_cuts", "false cuts", 0},
                        {"lost_share", "lost", 2},     {"camera_gated_share", "cam gated", 2},     {"loss_precision", "loss P", 2}, {"loss_recall", "loss R", 2},
                        {"pos_err_p95_px", "pos p95", 1}, {"jumps_over_60px", "jumps", 0}, {"ms_per_frame", "ms/frame", 2}, {"rss_mb", "RSS MB", 0}};
    std::ostringstream o;
    o << "| scene |";
    for (const Col& c : cols) o << " " << c.head << " |";
    o << "\n|---|";
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; ++i) o << "---:|";
    o << "\n";
    for (const std::string& s : order) {
        const auto& r = m.at(s);
        o << "| " << s << " |";
        for (const Col& c : cols) {
            const auto it = r.find(c.metric);
            o << " " << (it == r.end() || std::isnan(it->second) ? "-" : Num(it->second, c.digits)) << " |";
        }
        o << "\n";
    }
    o << "\nRuns per scene: " << opt.runs << " timed after " << opt.warmups << " warm-ups"
      << (opt.quick ? " (quick)" : "") << ". r = Pearson over 10 s windows (5 s hop) against the scene's ground truth.\n\n";
    o << "| scene | what |\n|---|---|\n";
    for (const std::string& s : order) o << "| " << s << " | " << what.at(s) << " |\n";
    return o.str();
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    bool runs_set = false, warmups_set = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "samn_bench: " << a << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            std::cout << kHelp;
            return 0;
        } else if (a == "--scenes") opt.scenes = Split(next());
        else if (a == "--out") opt.out = fs::u8path(next());
        else if (a == "--runs") opt.runs = std::max(1, std::atoi(next().c_str())), runs_set = true;
        else if (a == "--warmups") opt.warmups = std::max(0, std::atoi(next().c_str())), warmups_set = true;
        else if (a == "--quick") opt.quick = true;
        else if (a == "--no-track") opt.no_track = true;
        else if (a == "--timing-only") opt.timing_only = true;
        else if (a == "--keep-clips") opt.keep = true;
        else if (a == "--require-r") opt.require_r = std::atof(next().c_str());
        else if (a == "--list") opt.list = true;
        else {
            std::cerr << "samn_bench: unknown option " << a << "\n" << kHelp;
            return 2;
        }
    }
    if (opt.quick) {
        if (!runs_set) opt.runs = 1;
        if (!warmups_set) opt.warmups = 0;
    }
    const std::vector<SceneDef> all = Scenes();
    if (opt.list) {
        for (const SceneDef& d : all) std::cout << d.name << "\t" << d.what << (d.long_scene ? " (long)" : "") << "\n";
        return 0;
    }
    std::vector<const SceneDef*> chosen;
    if (opt.scenes.empty()) {
        for (const SceneDef& d : all)
            if (!(opt.quick && d.long_scene)) chosen.push_back(&d);
    } else {
        for (const std::string& n : opt.scenes) {
            const auto it = std::find_if(all.begin(), all.end(), [&](const SceneDef& d) { return d.name == n; });
            if (it == all.end()) {
                std::cerr << "samn_bench: no scene named " << n << " (--list shows them)\n";
                return 2;
            }
            chosen.push_back(&*it);
        }
    }
    fs::create_directories(opt.out / "clips");

    std::vector<Row> csv;
    std::vector<std::string> order;
    std::map<std::string, std::map<std::string, double>> table;
    std::map<std::string, std::string> what;
    int failed = 0;
    for (const SceneDef* d : chosen) {
        std::cerr << "scene " << d->name << " ...\n";
        const fs::path rows_file = opt.out / "clips" / (d->name + "_rows.csv");
        std::string error;
        Rows rows;
        double rss_mb = 0;
#if defined(__linux__)
        // One process per scene, so that the peak RSS is the scene's own.
        std::cout.flush();
        std::cerr.flush();
        const pid_t pid = fork();
        if (pid == 0) {
            std::string e;
            const Rows r = MeasureScene(*d, opt, &e);
            std::ostringstream o;
            for (const auto& kv : r) o << kv.first << "," << Num(kv.second, 6) << "\n";
            if (!e.empty()) o << "#error," << e << "\n";
            samn::WriteFileAtomic(rows_file, o.str());
            _exit(0);
        }
        int status = 0;
        struct rusage ru {};
        if (pid < 0 || wait4(pid, &status, 0, &ru) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error = "the scene process failed";
        } else {
            rss_mb = double(ru.ru_maxrss) / 1024.0;  // KiB on Linux
            std::ifstream in(rows_file);
            std::string line;
            while (std::getline(in, line)) {
                const size_t c = line.find(',');
                if (c == std::string::npos) continue;
                if (line.rfind("#error,", 0) == 0) error = line.substr(c + 1);
                else rows.push_back({line.substr(0, c), std::atof(line.c_str() + c + 1)});
            }
        }
        std::error_code ec;
        fs::remove(rows_file, ec);
#else
        rows = MeasureScene(*d, opt, &error);  // peak RSS is not measured here: 0
#endif
        rows.push_back({"rss_mb", rss_mb});
        if (!error.empty()) {
            ++failed;
            std::cerr << "  error: " << error << "\n";
        }
        order.push_back(d->name);
        what[d->name] = d->what;
        for (const auto& kv : rows) {
            csv.push_back({d->name, kv.first, kv.second});
            table[d->name][kv.first] = kv.second;
        }
    }

    std::ostringstream c;
    c << "scene,metric,value\n";
    for (const Row& r : csv) c << r.scene << "," << r.metric << "," << Num(r.value, Digits(r.metric)) << "\n";
    samn::WriteFileAtomic(opt.out / "samn_bench.csv", c.str());
    const std::string md = Markdown(order, table, what, opt);
    samn::WriteFileAtomic(opt.out / "samn_bench.md", md);
    std::cout << md;

    if (opt.require_r >= 0)
        for (const std::string& s : order) {
            const auto it = table[s].find("r_median");
            if (it == table[s].end() || std::isnan(it->second) || it->second < opt.require_r) {
                std::cerr << "samn_bench: " << s << " r_median "
                          << (it == table[s].end() ? "missing" : Num(it->second, 3)) << " is below " << opt.require_r << "\n";
                return 3;
            }
        }
    return failed ? 1 : 0;
}
