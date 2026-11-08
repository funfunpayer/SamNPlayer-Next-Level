#include "samn/audio_autosync.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace samn {
namespace {

constexpr int64_t kSmoothMs = 250;
constexpr int64_t kMaxLagMs = 3000;
constexpr int64_t kPeakGapMs = 300;
constexpr double kHighR = 0.30;
constexpr double kHighMargin = 0.08;
constexpr double kLowR = 0.12;

std::vector<double> Smooth(const std::vector<double>& in, int step_ms) {
    if (in.empty() || step_ms <= 0) return in;
    const int half = std::max(0, int(kSmoothMs / step_ms) / 2);
    if (half == 0) return in;
    std::vector<double> out(in.size(), 0.0);
    for (size_t i = 0; i < in.size(); ++i) {
        const size_t a = i > size_t(half) ? i - size_t(half) : 0;
        const size_t b = std::min(in.size(), i + size_t(half) + 1);
        double sum = 0;
        for (size_t j = a; j < b; ++j) sum += in[j];
        out[i] = sum / double(b - a);
    }
    return out;
}

// Stroke motion energy on the loudness grid: |Δpos| impulses at each action
// (constant-speed segments are flat after abs and do not correlate with audio).
std::vector<double> StrokeSpeedSeries(const std::vector<Action>& stroke, int step_ms, size_t n) {
    std::vector<double> speed(n, 0.0);
    if (stroke.size() < 2 || step_ms <= 0 || n == 0) return speed;
    for (size_t i = 1; i < stroke.size(); ++i) {
        const int64_t t1 = stroke[i].at;
        if (t1 < 0) continue;
        const size_t k = size_t(t1 / step_ms);
        if (k >= n) continue;
        speed[k] += std::abs(double(stroke[i].pos - stroke[i - 1].pos));
    }
    double peak = 0;
    for (double v : speed) peak = std::max(peak, v);
    if (peak > 0)
        for (double& v : speed) v /= peak;
    return speed;
}

double CorrelateAtLag(const std::vector<double>& a, const std::vector<double>& b, int lag_buckets) {
    // a[t] vs b[t - lag]: lag>0 means b leads a (script earlier than video sound).
    double mean_a = 0, mean_b = 0;
    int n = 0;
    const int na = int(a.size()), nb = int(b.size());
    for (int i = 0; i < na; ++i) {
        const int j = i - lag_buckets;
        if (j < 0 || j >= nb) continue;
        mean_a += a[size_t(i)];
        mean_b += b[size_t(j)];
        ++n;
    }
    if (n < 8) return 0;
    mean_a /= n;
    mean_b /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < na; ++i) {
        const int j = i - lag_buckets;
        if (j < 0 || j >= nb) continue;
        const double xa = a[size_t(i)] - mean_a;
        const double xb = b[size_t(j)] - mean_b;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    if (!(da > 0) || !(db > 0)) return 0;
    return num / std::sqrt(da * db);
}

}  // namespace

AudioSyncSuggestion EstimateAudioSyncOffset(const std::vector<double>& loudness, int step_ms,
                                            const std::vector<Action>& stroke, int64_t max_ms) {
    AudioSyncSuggestion out;
    if (step_ms <= 0 || loudness.size() < 8 || stroke.size() < 2) {
        out.note = "Need a script and video sound to suggest an offset.";
        return out;
    }
    const size_t max_n = size_t(std::max<int64_t>(0, max_ms) / step_ms);
    const size_t n = std::min(loudness.size(), max_n > 0 ? max_n : loudness.size());
    if (n < 8) {
        out.note = "The sound is too short to compare.";
        return out;
    }
    std::vector<double> loud(loudness.begin(), loudness.begin() + int64_t(n));
    auto speed = StrokeSpeedSeries(stroke, step_ms, n);
    loud = Smooth(loud, step_ms);
    speed = Smooth(speed, step_ms);

    const int max_lag = int(kMaxLagMs / step_ms);
    const int peak_gap = std::max(1, int(kPeakGapMs / step_ms));
    double best_r = -2, second_r = -2;
    int best_lag = 0;
    for (int lag = -max_lag; lag <= max_lag; ++lag) {
        const double r = CorrelateAtLag(loud, speed, lag);
        if (r > best_r) {
            if (std::abs(lag - best_lag) >= peak_gap) second_r = best_r;
            best_r = r;
            best_lag = lag;
        } else if (r > second_r && std::abs(lag - best_lag) >= peak_gap) {
            second_r = r;
        }
    }
    out.correlation = best_r;
    out.offset_ms = int64_t(best_lag) * int64_t(step_ms);
    const double margin = best_r - (second_r > -1 ? second_r : 0);
    if (best_r >= kHighR && margin >= kHighMargin) {
        out.confidence = AudioSyncConfidence::High;
        out.note = "Suggested offset " + std::to_string(out.offset_ms) + " ms (high confidence).";
    } else if (best_r >= kLowR) {
        out.confidence = AudioSyncConfidence::Low;
        out.note = "Suggested offset " + std::to_string(out.offset_ms) + " ms (low confidence).";
    } else {
        out.confidence = AudioSyncConfidence::None;
        out.offset_ms = 0;
        out.note = "No clear match between the sound and the stroke.";
    }
    return out;
}

}  // namespace samn
