#pragma once

// The self-checking engine (docs/P3_SELF_CHECK_ENGINE.md), slices SC-1 and
// SC-3: pure maths on the writer's track and per-frame checker signals, no
// OpenCV. The CSRT box stays the only writer (GD-01); nothing here invents a
// position. It marks samples invalid, interpolates only short gaps, and says
// per window whether the writer can be trusted and why.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace samn::engine {

// ---------- SC-1: sample gate (the document's discipline) ----------

struct SampleGateOptions {
    int hampel_k = 3;               // neighbours on each side
    double hampel_factor = 5.0;     // outlier when |y - median| > factor * (1.4826 * MAD + eps)
    double hampel_eps = 0.5;        // pixels; keeps a perfectly flat stretch from flagging noise
    double max_interp_gap_ms = 500; // invalid runs up to this long are interpolated, longer ones split
    int min_segment_samples = 10;   // shorter valid segments are dropped (marked invalid)
};

struct SampleGateResult {
    std::vector<double> positions;          // gated; NaN where invalid
    std::vector<uint8_t> valid;             // 1 = usable sample
    std::vector<std::pair<int, int>> segments;  // [start, end) sample indices of continuous valid runs
    int outliers = 0;                       // samples rejected by Hampel
    int interpolated = 0;                   // samples filled inside short gaps
    int dropped = 0;                        // samples lost to long gaps or short segments
};

// Hampel outlier mask over finite samples; NaN samples are never outliers.
std::vector<uint8_t> HampelMask(const std::vector<double>& y, int k, double factor, double eps);

// positions: the writer's track, NaN where the tracker had no box or a
// checker failed the sample. times_ms: same length, increasing.
SampleGateResult GateSamples(const std::vector<double>& times_ms, const std::vector<double>& positions,
                             const SampleGateOptions& options);

// ---------- SC-3: window verifier ----------

// Per-frame checker signals, all the same length as the track. An empty
// vector means "this checker did not run": it is unknown, never a pass.
struct FrameChecks {
    std::vector<double> box_flow;     // c1: vertical flow in the box minus the camera (outside median), px/frame
    std::vector<double> foreground;   // c2: foreground fraction inside the box (0..1)
    std::vector<uint8_t> tracker_ok;  // c4: 1 = the tracker returned a box
    std::vector<double> camera_inliers;  // c6: RANSAC inlier fraction of the camera estimate (0..1)
    std::vector<double> appearance;      // c7: match against the original seed (0..1)
    // Fast lane: foreground fraction of the whole (small, stabilised) frame.
    // Tells a pause (nothing moves) from a box that lost its target.
    std::vector<double> scene_motion;
};

struct VerifyOptions {
    int window_frames = 60;          // 2 s at 30 fps
    int skip_frames = 0;             // frames at the start without a verdict (model warm-up)
    // Scene cuts (frame index of the first frame of each new shot, from
    // scene_cut or the fast lane). Windows never span a cut; the first
    // cut_guard_frames of a shot get no verdict (flow and background models
    // see the cut itself as motion). A shot's last part shorter than half a
    // window is joined to the window before it.
    std::vector<int> cuts;
    int cut_guard_frames = 5;
    double c1_min = 0.4;             // flow agreement (correlation)
    double c2_min = 0.3;             // foreground relative to the reference
    double c3_max = 0.1;             // Hampel rate
    double c4_min = 0.7;             // tracker ok-rate
    double c6_min = 0.4;             // camera trust; below it c1 is unknown
    double c7_min = 0.5;             // appearance against the original seed
    double c8_min = 0.25;            // range relative to the typical window range (75th percentile
                                     // over the clip, so it holds while up to 3/4 of windows are broken)
    double still_flow = 0.3;         // px/frame: median |box_flow| under this is quiet
    double still_scene = 0.02;       // scene_motion under this is a quiet scene
    double foreground_ref = 0;       // 0 = median of c2 over the first 10 windows of the clip
    SampleGateOptions hampel;        // c3 uses the same Hampel rule as the gate
};

enum class WindowState { kPass, kSuspect, kFail, kStill };

struct WindowVerdict {
    int first = 0, end = 0;          // [first, end) frame indices
    WindowState state = WindowState::kPass;
    // NaN = unknown (checker did not run or had too little data)
    double c1 = 0, c2 = 0, c3 = 0, c4 = 0, c6 = 0, c7 = 0, c8 = 0;
    std::string reasons;             // e.g. "c1 flow 0.05 < 0.4; c4 ok 0.20 < 0.7"
};

const char* WindowStateName(WindowState s);

// Pearson correlation over pairs where both are finite; NaN with fewer than
// 10 pairs or no spread.
double WindowCorrelation(const std::vector<double>& a, const std::vector<double>& b);

// positions: the writer's track in image pixels (y down or x), NaN where lost.
std::vector<WindowVerdict> VerifyWindows(const std::vector<double>& positions, const FrameChecks& checks,
                                         const VerifyOptions& options);

}  // namespace samn::engine
