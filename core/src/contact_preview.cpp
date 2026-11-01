#include "samn/contact_preview.h"

#include <algorithm>
#include <cmath>

#include "samn/mapper.h"

namespace samn {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int64_t kProbeDurationMs = 3000;
constexpr int64_t kProbeStepMs = 50;
// Bounce ~0.75 Hz between 15 and 95 so deep-only vs earlier is obvious.
constexpr double kBounceHz = 0.75;
constexpr double kBounceMid = 55.0;
constexpr double kBounceAmp = 40.0;
constexpr double kPeakFloor = 0.15;  // 0.7 peak dots: local max >= 15

}  // namespace

std::vector<Action> SyntheticContactBounce() {
    std::vector<Action> out;
    out.reserve(static_cast<size_t>(kProbeDurationMs / kProbeStepMs) + 1);
    for (int64_t t = 0; t <= kProbeDurationMs; t += kProbeStepMs) {
        const double phase = 2.0 * kPi * kBounceHz * (static_cast<double>(t) / 1000.0);
        const int pos = static_cast<int>(std::lround(kBounceMid + kBounceAmp * std::sin(phase)));
        out.push_back({t, std::clamp(pos, 0, 100)});
    }
    return out;
}

ContactVibrationPreview PreviewContactVibration(double span, const std::string& curve, double strength) {
    ContactVibrationPreview out;
    out.hint = "Depth window for contact vibe — earlier = more of the stroke.";
    const std::vector<Action> bounce = SyntheticContactBounce();
    if (bounce.size() < 2) return out;

    MapOptions opts = DefaultMapOptions();
    opts.contact_vibration = true;
    opts.contact_vibration_span = EffectiveContactSpan(span);
    opts.contact_vibration_curve = NormalizeContactCurve(curve);
    // SuctionPosition: vibration is the contact depth envelope only (no
    // stroke-speed intensity fallback), so Sensitivity / Curve stay visible.
    opts.sync = SyncMode::SuctionPosition;
    opts.min_vibration = 0;  // raw envelope for the probe; Create still lifts
    opts.smoothing = 0;
    const auto frames = ToIntensityCurve(bounce, opts);
    if (!frames || frames->size() < 2) return out;

    const double scale = std::clamp(strength, 0.0, 2.0);
    const size_t n = frames->size();
    const double denom = static_cast<double>(n > 1 ? n - 1 : 1);
    out.stroke.reserve(n);
    out.vib.reserve(n);
    size_t active = 0;
    double peak = 0;
    for (size_t i = 0; i < n; ++i) {
        const Frame& f = (*frames)[i];
        // Stroke at the same ticks (interpolate the bounce).
        const double pos = SampleAxisPos(bounce, f.at) / 100.0;
        const double vib = Clamp01(f.vibration * scale);
        const double x = static_cast<double>(i) / denom;
        out.stroke.push_back({x, Clamp01(pos)});
        out.vib.push_back({x, vib});
        if (vib > 1e-6) ++active;
        peak = std::max(peak, vib);
    }
    out.active_pct = 100.0 * static_cast<double>(active) / static_cast<double>(n);
    out.peak = 100.0 * peak;

    for (size_t i = 1; i + 1 < n; ++i) {
        const double y = out.vib[i].y;
        if (y < kPeakFloor) continue;
        if (y >= out.vib[i - 1].y && y >= out.vib[i + 1].y) out.peaks.push_back(out.vib[i]);
    }
    return out;
}

}  // namespace samn
