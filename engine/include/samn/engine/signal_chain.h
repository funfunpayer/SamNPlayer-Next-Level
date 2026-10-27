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
// Profile "recommended" (D-159, the Generator's default): Autotune's recipe
// with these spec values; a value set in SignalOptions still wins.
constexpr double kRecommendedMadFactor = 5, kRecommendedMaxSpeed = 500, kRecommendedIntervalMs = 60;

struct SignalOptions {
    std::string profile;  // "", standard, weich, autotune, recommended, tf, tj, ...
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
    // Opt-in, 0 = off (0.7 behaviour). max_gap_ms: lost stretches longer
    // than this split the track and get no actions; shorter ones are bridged.
    // outlier_mad_factor: single samples this many local MADs off are
    // treated as lost (spec default 5).
    double max_gap_ms = 0;
    double outlier_mad_factor = 0;
    // Gaps come from the tracker's own loss (kLostTracker). The look check
    // (kLostUnlike) counts only when this is on: on the Owner's clip it
    // marked 21 % of frames and gap handling then lost 0.695 -> 0.50 (D-161).
    bool gaps_from_look = false;
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
    PostResult post;              // dense curve for the Quality Doctor (without split gaps)
    int outliers = 0, bridged_gaps = 0, split_gaps = 0;
};

std::optional<SignalResult> RunSignal(const std::vector<double>& times_ms, const std::vector<double>& positions,
                                      const SignalOptions& o, std::string* error);
// lost: TrackResult::lost (1 = no measurement); used only with max_gap_ms.
std::optional<SignalResult> RunSignal(const std::vector<double>& times_ms, const std::vector<double>& positions,
                                      const std::vector<uint8_t>& lost, const SignalOptions& o, std::string* error);

// Strokes faster than this share of the sample rate cannot be followed
// reliably (two samples per half stroke at most): the result is a warning.
constexpr double kAliasingShare = 0.4;

// The stroke rate of the raw track (detrended crossings with hysteresis)
// against its sample rate; a warning text when the rate is at or above
// kAliasingShare x the sample rate, else nullopt. Never changes the script.
std::optional<std::string> AliasingWarning(const std::vector<double>& times_ms, const std::vector<double>& positions);

}  // namespace samn::engine
