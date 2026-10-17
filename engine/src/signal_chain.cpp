#include "samn/engine/signal_chain.h"

#include <algorithm>
#include <cstdio>

#include "samn/samn_file.h"

namespace samn::engine {

namespace {
// Measured on a 280 s clip against two references: 62 % of 10 s windows sat
// in a band under 25 points without detrend; windowed r went 0.275/0.261 to
// 0.386/0.552 with a rolling mean of two stroke periods (LESSONS GD-12).
constexpr double kDetrendFallbackMs = 3000, kDetrendMinMs = 1500, kDetrendMaxMs = 4000;
}  // namespace

double AdaptiveDetrendMs(const std::optional<StrokeHint>& hint, std::string* why) {
    auto say = [&](const std::string& s) {
        if (why) *why = s;
    };
    if (!hint || !hint->reliable) {
        say("no reliable stroke tempo, default window");
        return kDetrendFallbackMs;
    }
    if (hint->stroke_hz < 0.3 || hint->stroke_hz > 3.5) {
        say("stroke tempo out of range, default window");
        return kDetrendFallbackMs;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "2x stroke period at %.2f Hz", hint->stroke_hz);
    say(buf);
    return std::clamp(2 * 1000 / hint->stroke_hz, kDetrendMinMs, kDetrendMaxMs);
}

SignalOptions ApplyDefaultDetrend(SignalOptions o, const std::optional<StrokeHint>& hint, std::string* note) {
    if (o.detrend_window_ms != 0 || IsDistanceProfile(o.profile)) return o;
    std::string why;
    o.detrend_window_ms = AdaptiveDetrendMs(hint, &why);
    if (note) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "POST: detrend %.0fms (", o.detrend_window_ms);
        *note = buf + why + ")";
    }
    return o;
}

PostOptions BuildPostOptions(const SignalOptions& o, int smooth, int peak_dist, double adaptive, double norm_pct) {
    PostOptions p;
    p.invert = o.invert;
    if (smooth > 0) p.smooth_window = smooth;
    if (peak_dist > 0) p.min_peak_distance_ms = peak_dist;
    p.rdp_tolerance = o.rdp_tolerance;
    if (norm_pct > 0)
        p.norm_percentile = norm_pct;
    else if (norm_pct < 0)
        p.norm_percentile = 0;
    p.adaptive_error = adaptive;
    if (o.min_action_interval_ms > 0)
        p.min_interval_ms = o.min_action_interval_ms;
    else if (o.min_action_interval_ms < 0)
        p.min_interval_ms = 0;
    p.dynamic_range_ms = o.dynamic_range_ms;
    p.peak_prominence = o.peak_prominence;
    p.detrend_window_ms = o.detrend_window_ms;
    p.bandpass_low_hz = o.bandpass_low_hz;
    p.bandpass_high_hz = o.bandpass_high_hz;
    if (o.profile == "weich") {
        if (p.peak_prominence == 0) p.peak_prominence = 0.35;
        if (peak_dist == 0) p.min_peak_distance_ms = 200;
        if (p.dynamic_range_ms == 0) p.dynamic_range_ms = 3000;
    }
    if (o.profile == "autotune") {
        // Multi-filter recipe after FunGen's Ultimate Autotune and
        // Funscript-Flow; the classical track still writes the script.
        if (p.detrend_window_ms == 0) p.detrend_window_ms = 3000;
        if (p.bandpass_low_hz == 0 && p.bandpass_high_hz == 0) {
            p.bandpass_low_hz = 0.5;
            p.bandpass_high_hz = 4.0;
        }
        if (p.dynamic_range_ms == 0) p.dynamic_range_ms = 3000;
        if (p.peak_prominence == 0) p.peak_prominence = 0.2;
    }
    return p;
}

std::optional<SignalResult> RunSignal(const std::vector<double>& t, const std::vector<double>& y,
                                      const SignalOptions& o, std::string* error) {
    auto post = PositionsToActions(
        t, y, BuildPostOptions(o, o.smooth_window, o.min_peak_distance_ms, o.adaptive_error, o.norm_percentile), error);
    if (!post) return std::nullopt;
    SignalResult r;
    r.actions = post->actions;
    const double max_speed = o.max_speed > 0 ? o.max_speed : o.profile == "autotune" ? 400 : 0;
    if (max_speed > 0) r.actions = LimitSpeed(r.actions, max_speed, nullptr);
    if (IsDistanceProfile(o.profile)) r.actions = ClampActionsPos(r.actions, 20, 90);
    r.post = std::move(*post);
    return r;
}

}  // namespace samn::engine
