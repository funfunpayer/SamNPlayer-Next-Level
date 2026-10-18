#pragma once

// Post-processing (P3.2): the raw track becomes funscript actions. Port of
// 0.7's posttrack (itself the port of generate_funscript.py), same stages in
// the same order: Savitzky-Golay → detrend → bandpass → dynamic-range lift →
// percentile normalisation 0-100 → peaks / valleys with prominence →
// adaptive keyframes → minimum interval → RDP. Speed limit and clamp are
// separate steps.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "samn/funscript.h"

namespace samn::engine {

struct PostOptions {
    bool invert = false;
    int smooth_window = 11;
    int min_peak_distance_ms = 150;
    double rdp_tolerance = 0;
    double norm_percentile = 2.0;          // 0 = min/max
    std::vector<std::pair<int, int>> scene_ranges;  // [start, end) sample indices, normalised each
    double adaptive_error = 0;
    double min_interval_ms = 100.0;        // <= 0 = off
    double dynamic_range_ms = 0;
    double peak_prominence = 0;            // fraction of the position span; 0 = off
    double detrend_window_ms = 0;          // rolling-mean high-pass; 0 = off
    double bandpass_low_hz = 0, bandpass_high_hz = 0;  // 0 = that edge off
};

struct PostResult {
    std::vector<Action> actions;
    // The dense normalised curve: the Quality Doctor needs it, because
    // keyframes alone look rhythmic even when the tracker wandered.
    std::vector<double> dense_at, dense_pos;
};

// nullopt with *error set on mismatched input or a curve without motion.
std::optional<PostResult> PositionsToActions(const std::vector<double>& times_ms, const std::vector<double>& positions,
                                             const PostOptions& options, std::string* error);

// Caps the position change between actions at max_units_per_s; returns the
// number of actions changed. max <= 0 or fewer than 2 actions: unchanged.
std::vector<Action> LimitSpeed(const std::vector<Action>& actions, double max_units_per_s, int* changed);

std::vector<Action> ClampActionsPos(const std::vector<Action>& actions, int lo, int hi);

// Building blocks, public for tests and the Editor.
std::vector<double> RollingDetrend(const std::vector<double>& values, double window_ms, double step_ms);
std::vector<double> Bandpass(const std::vector<double>& values, double sample_hz, double low_hz, double high_hz);
// scipy.signal.find_peaks subset: local maxima (plateau middle), minimum
// distance in samples (taller first), minimum prominence.
std::vector<int> FindPeaks(const std::vector<double>& y, int distance, double prominence);
// numpy percentile with linear interpolation.
double Percentile(std::vector<double> values, double p);

}  // namespace samn::engine
