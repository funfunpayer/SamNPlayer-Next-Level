#include "samn/output_levels.h"

#include <algorithm>
#include <cmath>

namespace samn {

namespace {

double Unit(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; }

// Step i (1..kRampSteps) of a ramp of `durationMs` at `elapsedMs`, as the
// fraction i / kRampSteps.
double StepFraction(int64_t elapsedMs, int64_t durationMs) {
    const int64_t i = std::min<int64_t>(kRampSteps, elapsedMs * kRampSteps / durationMs + 1);
    return double(i) / kRampSteps;
}

}  // namespace

double ReducePeak(double v) {
    if (!std::isfinite(v)) return 0;
    if (v <= kPeakKnee) return v;
    return kPeakKnee + (v - kPeakKnee) * kPeakRatio;
}

double OutputLevel(double value, bool reducePeaks, double scale, double level) {
    double v = Unit(value);
    if (reducePeaks) v = ReducePeak(v);
    return v * Unit(scale) * Unit(level);
}

double SoftStartFactor(int64_t elapsedMs, int64_t durationMs) {
    if (durationMs <= 0 || elapsedMs >= durationMs) return 1;
    return StepFraction(std::max<int64_t>(0, elapsedMs), durationMs);
}

bool SoftStartRunning(int64_t elapsedMs, int64_t durationMs) { return durationMs > 0 && elapsedMs < durationMs; }

ExtendedOOptions Clamped(ExtendedOOptions o) {
    o.minLevel = Unit(o.minLevel);
    o.holdMs = std::max<int64_t>(0, o.holdMs);
    o.restoreMs = std::max<int64_t>(0, o.restoreMs);
    return o;
}

int64_t ExtendedOTotalMs(const ExtendedOOptions& o) {
    const ExtendedOOptions c = Clamped(o);
    return 2 * c.restoreMs + c.holdMs;
}

double ExtendedOScale(const ExtendedOOptions& o, int64_t t) {
    const ExtendedOOptions c = Clamped(o);
    if (t < 0 || t >= ExtendedOTotalMs(c)) return 1;
    const double lo = c.minLevel;
    if (t < c.restoreMs) return 1 + StepFraction(t, c.restoreMs) * (lo - 1);  // down
    t -= c.restoreMs;
    if (t < c.holdMs) return lo;  // hold
    t -= c.holdMs;
    return lo + StepFraction(t, c.restoreMs) * (1 - lo);  // back up
}

}  // namespace samn
