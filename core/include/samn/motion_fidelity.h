#pragma once
// Motion Fidelity: how well a candidate script follows a reference (rhythm,
// phase, amplitude). Port of SamNPlayer 0.7 funscript/phase.go. Kept apart
// from the Quality Doctor on purpose: the two axes do not predict each other
// (LESSONS GD-04).
//
// The lessons of 0.7's benchmark (GD-05, GD-06) are built in: one absolute
// timeline, +-1000 ms lag window, normal and inverted orientation, undefined
// instead of 0 for a constant series, a low-confidence flag under 30
// samples, and an aliasing flag that never changes the reported lag.
#include <optional>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

constexpr int kDefaultMaxLagMs = 1000;
constexpr int kDefaultLagStepMs = 100;
constexpr int kDefaultResampleStepMs = 100;
constexpr int kMinOverlapSamples = 10;
constexpr int kLowConfidenceSamples = 30;
constexpr double kDefaultTimingGainMin = 0.30;  // aligned r must beat raw by this -> timing
constexpr double kDefaultMinAlignedR = 0.70;    // below this after alignment -> shape
constexpr int kDefaultWindowMs = 30000;
constexpr double kDefaultAliasingEpsilon = 0.02;
constexpr int kMinDominantPeriodMs = 150;
constexpr int kMaxDominantPeriodMs = 2000;

struct LagCorrelation {
    double r = 0;
    int lag_ms = 0;
    std::string orientation;  // "normal" or "inverted"
    int n_samples = 0;
    std::optional<double> shape_error;
    std::optional<double> r_zero_lag;
    bool low_confidence = false;
    // Another lag near best +- k x period scores within 0.02 of r. lag_ms and
    // r stay as found: a flag plus a human look, no tie-breaking (D-065).
    bool aliasing_risk = false;
    int dominant_period_ms = 0;
    std::vector<int> alternate_lags_ms;
};

// Best lag and orientation of b against a on a shared absolute timeline.
// nullopt when no overlap of 10+ samples exists or Pearson is undefined at
// every lag. Negative max_lag_ms and non-positive steps take the defaults.
std::optional<LagCorrelation> BestLagCorrelation(const std::vector<Action>& a, const std::vector<Action>& b,
                                                 int max_lag_ms = kDefaultMaxLagMs, int lag_step_ms = kDefaultLagStepMs,
                                                 int resample_step_ms = kDefaultResampleStepMs);

struct WindowResult {
    int64_t start_ms = 0, end_ms = 0;
    std::optional<LagCorrelation> correlation;
    std::string reason;  // "too_few_actions_in_window" | "undefined_correlation" when empty
};

// Fixed windows over a's timeline, each searched on its own (b padded by the
// lag window). Quiet windows stay as rows with a reason, never dropped.
std::vector<WindowResult> WindowedBestLagCorrelation(const std::vector<Action>& a, const std::vector<Action>& b,
                                                     int window_ms = kDefaultWindowMs,
                                                     int max_lag_ms = kDefaultMaxLagMs,
                                                     int lag_step_ms = kDefaultLagStepMs,
                                                     int resample_step_ms = kDefaultResampleStepMs);

enum class PhaseVerdict { kUndefined, kTiming, kShape, kOk };
const char* PhaseVerdictName(PhaseVerdict v);

struct PhaseDiagnosis {
    PhaseVerdict verdict = PhaseVerdict::kUndefined;
    std::string detail;
};

// Timing (aligned r beats raw by >= 0.30) vs shape (aligned r < 0.70) vs ok.
PhaseDiagnosis DiagnosePhase(const std::optional<LagCorrelation>& corr, double timing_gain_min = 0,
                             double min_aligned_r = 0);

// Dominant stroke period from the autocorrelation's first genuine local peak
// after the initial decline (150-2000 ms); 0 when there is none.
int DominantPeriodMs(const std::vector<Action>& actions, int step_ms);

// Pearson r with population std; nullopt on zero variance, never 0.
std::optional<double> Pearson(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace samn
