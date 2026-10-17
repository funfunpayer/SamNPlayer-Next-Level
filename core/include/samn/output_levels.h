#pragma once
// What the vibration and suction levels go through on their way to the
// device while a script plays, as functions of time without timers:
// Reduce peaks, the soft start ramp and the Extended-O scale schedule.
// Go reference: player/player.go (ReducePeak, setOutput), player/sync.go
// (softStartWithPositions), player/extendedo.go (startExtendedO, rampScale).
#include <cstdint>

namespace samn {

// Reduce peaks: values up to kPeakKnee pass unchanged, everything above is
// compressed by kPeakRatio, so full strength (1.0) comes out at 0.76.
constexpr double kPeakKnee = 0.6;
constexpr double kPeakRatio = 0.4;
// One output value 0-1; a value that is not finite is 0.
double ReducePeak(double v);

// The level sent for a script value (0-1): Reduce peaks first (when on),
// then times the Extended-O scale and the channel level (both 0-1), as
// 0.7 setOutput. Every input is clamped to 0-1; a non-finite input is 0.
double OutputLevel(double value, bool reducePeaks, double scale, double level);

// Ramps (soft start, Extended-O) run in this many equal steps (0.7).
constexpr int kRampSteps = 10;

// Soft start: the factor (0.1 ... 1) the levels are multiplied with at
// `elapsedMs` after the ramp began. Step i (1-10) holds from (i-1)/10 to
// i/10 of the duration, so the first step goes out at once. A duration of
// 0 or less is off (1 at once).
double SoftStartFactor(int64_t elapsedMs, int64_t durationMs);
// True while the soft start ramp is still running at `elapsedMs`.
bool SoftStartRunning(int64_t elapsedMs, int64_t durationMs);

// Extended-O: the script curve keeps running, vibration and suction are
// multiplied by a scale that ramps 1 -> minLevel over restoreMs, holds for
// holdMs and ramps back over restoreMs. minLevel 0 is "off", a real value.
struct ExtendedOOptions {
    double minLevel = 0.1;      // 0-1
    int64_t holdMs = 10000;     // >= 0
    int64_t restoreMs = 500;    // >= 0; 0 switches at once
};
// minLevel clamped to 0-1 (not finite: 0, the safe side), times not below 0.
ExtendedOOptions Clamped(ExtendedOOptions o);
// Whole cycle: two ramps and the hold.
int64_t ExtendedOTotalMs(const ExtendedOOptions& o);
// The scale at `elapsedMs` after the trigger; 1 before 0 and from the end
// of the cycle on.
double ExtendedOScale(const ExtendedOOptions& o, int64_t elapsedMs);

}  // namespace samn
