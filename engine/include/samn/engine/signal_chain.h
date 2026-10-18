#pragma once

// The signal half of the generator (P3.2): profile recipes, the default
// detrend for stroke profiles (GD-12), post-processing, speed limit and the
// distance-profile clamp. Port of 0.7 generator finishNativeGenerate's
// buildPost / runSignal and detrend_default.go. Quality Doctor and the
// signal auto-retry follow with P3.6.

#include <optional>
#include <string>
#include <vector>

#include "samn/engine/post_track.h"

namespace samn::engine {

// The user's choices. 0 means "the profile's or the default value"; a
// negative norm_percentile, min_action_interval_ms or detrend_window_ms
// means explicitly off.
struct SignalOptions {
    std::string profile;  // "", standard, weich, autotune, tf, tj, ...
    bool invert = false;
    int smooth_window = 0;
    int min_peak_distance_ms = 0;
    double adaptive_error = 0;
    double norm_percentile = 0;
    double min_action_interval_ms = 0;
    double dynamic_range_ms = 0;
    double peak_prominence = 0;
    double detrend_window_ms = 0;
    double bandpass_low_hz = 0, bandpass_high_hz = 0;
    double rdp_tolerance = 0;
    double max_speed = 0;  // units per second; autotune defaults to 400
};

// The stroke tempo from the stroke preview, when it was reliable.
struct StrokeHint {
    bool reliable = false;
    double stroke_hz = 0;
};

// Two stroke periods, clamped to 1.5–4 s; 3 s without a reliable tempo in
// 0.3–3.5 Hz. *why says which.
double AdaptiveDetrendMs(const std::optional<StrokeHint>& hint, std::string* why);

// Turns detrend on for stroke profiles when the caller left it at 0.
// Distance profiles (tf, tj) stay as they are: there the absolute value is
// the signal (distance near 0 = contact).
SignalOptions ApplyDefaultDetrend(SignalOptions o, const std::optional<StrokeHint>& hint, std::string* note);

// The post-processing options for one try (the auto-retry varies smooth,
// peak distance, adaptive error and norm percentile).
PostOptions BuildPostOptions(const SignalOptions& o, int smooth, int peak_dist, double adaptive, double norm_pct);

struct SignalResult {
    std::vector<Action> actions;  // after speed limit and clamp
    PostResult post;              // dense curve for the Quality Doctor
};

std::optional<SignalResult> RunSignal(const std::vector<double>& times_ms, const std::vector<double>& positions,
                                      const SignalOptions& o, std::string* error);

}  // namespace samn::engine
