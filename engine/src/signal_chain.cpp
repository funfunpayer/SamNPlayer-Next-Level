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
    const bool recommended = o.profile == ProfileRecommended;
    if (o.profile == "autotune" || recommended) {
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
    // D-159 measured on 9 synthetic tracks with known strokes and the
    // Owner's clip (27 Oct): the spec's 60 ms spacing and 2nd/98th
    // percentile are neutral there and kept; its 120 ms smoothing and 4 s
    // detrend lost against Autotune's, so those stay as Autotune has them.
    if (recommended && o.min_action_interval_ms == 0) p.min_interval_ms = kRecommendedIntervalMs;
    return p;
}

std::optional<SignalResult> RunSignal(const std::vector<double>& t, const std::vector<double>& y,
                                      const SignalOptions& o, std::string* error) {
    return RunSignal(t, y, {}, o, error);
}

namespace {

// Each segment between split gaps is post-processed on its own (its own
// smoothing edges and normalisation); a segment without motion is left out.
std::optional<PostResult> PostSegments(const std::vector<double>& t, const GapPlan& plan, const PostOptions& po,
                                       std::string* error) {
    PostResult all;
    std::string first_error;
    for (const auto& [a, b] : plan.segments) {
        if (b - a < 2) continue;
        const std::vector<double> st(t.begin() + a, t.begin() + b), sy(plan.values.begin() + a, plan.values.begin() + b);
        std::string why;
        auto seg = PositionsToActions(st, sy, po, &why);
        if (!seg) {
            if (first_error.empty()) first_error = why;
            continue;
        }
        all.actions.insert(all.actions.end(), seg->actions.begin(), seg->actions.end());
        all.dense_at.insert(all.dense_at.end(), seg->dense_at.begin(), seg->dense_at.end());
        all.dense_pos.insert(all.dense_pos.end(), seg->dense_pos.begin(), seg->dense_pos.end());
    }
    if (all.actions.empty()) {
        if (error) *error = first_error.empty() ? "no usable tracked stretch: the tracker lost the region" : first_error;
        return std::nullopt;
    }
    return all;
}

}  // namespace

std::optional<SignalResult> RunSignal(const std::vector<double>& t, const std::vector<double>& y,
                                      const std::vector<uint8_t>& lost, const SignalOptions& o, std::string* error) {
    const PostOptions po =
        BuildPostOptions(o, o.smooth_window, o.min_peak_distance_ms, o.adaptive_error, o.norm_percentile);
    // Recommended (D-159): single-frame jumps are lost samples (synthetic
    // window r 0.957 -> 0.968; the Owner's clip unchanged).
    const double mad_factor = o.outlier_mad_factor > 0                     ? o.outlier_mad_factor
                              : o.outlier_mad_factor == 0 && o.profile == ProfileRecommended ? kRecommendedMadFactor
                                                                                             : 0;
    SignalResult r;
    std::optional<PostResult> post;
    if (o.max_gap_ms <= 0 && mad_factor <= 0) {
        post = PositionsToActions(t, y, po, error);
    } else {
        if (t.size() != y.size() || y.empty()) {
            if (error) *error = "timestamps and positions differ in length";
            return std::nullopt;
        }
        GapOptions go;
        go.max_gap_ms = o.max_gap_ms;
        go.outlier_mad_factor = mad_factor;
        // Without a gap limit a lost frame keeps its held value, as in 0.7.
        const GapPlan plan = PlanGaps(t, y, o.max_gap_ms > 0 ? lost : std::vector<uint8_t>{}, go);
        r.outliers = plan.outliers;
        r.bridged_gaps = plan.bridged;
        r.split_gaps = plan.split;
        post = PostSegments(t, plan, po, error);
    }
    if (!post) return std::nullopt;
    r.actions = post->actions;
    // Recommended: 500 units/s (spec FR-17) beat Autotune's 400 against the
    // known strokes (window r 0.968 -> 0.982).
    const double max_speed = o.max_speed > 0                   ? o.max_speed
                             : o.profile == "autotune"         ? 400
                             : o.profile == ProfileRecommended ? kRecommendedMaxSpeed
                                                               : 0;
    if (max_speed > 0) r.actions = LimitSpeed(r.actions, max_speed, nullptr);
    if (IsDistanceProfile(o.profile)) r.actions = ClampActionsPos(r.actions, 20, 90);
    r.post = std::move(*post);
    return r;
}

std::optional<std::string> AliasingWarning(const std::vector<double>& t, const std::vector<double>& y) {
    const size_t n = y.size();
    if (n < 8 || t.size() != n || t.back() <= t.front()) return std::nullopt;
    const double seconds = (t.back() - t.front()) / 1000.0;
    const double sample_hz = (n - 1) / seconds;
    // Remove the slow baseline over about a second, then count crossings of
    // the middle that clear a hysteresis band (codec noise is not a stroke).
    const std::vector<double> d = RollingDetrend(y, 1000.0, 1000.0 / sample_hz);
    const double band = (Percentile(d, 95) - Percentile(d, 5)) * 0.1;
    if (band <= 0) return std::nullopt;
    // The rate between the first and the last crossing: counting over the
    // whole clip would lose up to one crossing at each edge.
    std::vector<double> at;
    int side = 0;
    for (size_t i = 0; i < n; ++i) {
        const int s = d[i] > band ? 1 : d[i] < -band ? -1 : 0;
        if (s == 0) continue;
        if (side != 0 && s != side) at.push_back(t[i]);
        side = s;
    }
    if (at.size() < 3 || at.back() <= at.front()) return std::nullopt;
    const double stroke_hz = (at.size() - 1) / 2.0 / ((at.back() - at.front()) / 1000.0);
    if (stroke_hz < kAliasingShare * sample_hz) return std::nullopt;
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "analysis rate too low for this motion: about %.1f strokes/s at %.1f frames/s; the script may "
                  "miss strokes",
                  stroke_hz, sample_hz);
    return std::string(buf);
}

}  // namespace samn::engine
