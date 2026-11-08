#include "samn/audio_envelope.h"

#include <algorithm>
#include <cmath>

namespace samn {

WindowRms::WindowRms(size_t window) : window_(std::max<size_t>(1, window)) {}

void WindowRms::Push(float sample) {
    const double v = sample;
    sum_ += v * v;
    if (++n_ == window_) {
        out_.push_back(std::sqrt(sum_ / static_cast<double>(n_)));
        sum_ = 0;
        n_ = 0;
    }
}

void WindowRms::Push(const float* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) Push(samples[i]);
}

std::vector<double> WindowRms::Finish() const {
    std::vector<double> out = out_;
    if (n_ > 0 && n_ * 2 >= window_) out.push_back(std::sqrt(sum_ / static_cast<double>(n_)));
    return out;
}

namespace {

// Same index as 0.7: round(q * (n - 1)).
double Percentile(std::vector<double>& v, double q) {
    const auto idx = static_cast<size_t>(std::llround(q * static_cast<double>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(idx), v.end());
    return v[idx];
}

}  // namespace

std::vector<double> NormalizeLoudness(const std::vector<double>& rms) {
    std::vector<double> out(rms.size(), 0.0);
    std::vector<double> db;
    db.reserve(rms.size());
    for (double v : rms)
        if (v >= kAudioSilenceRms) db.push_back(20 * std::log10(v));
    if (db.empty()) return out;
    double lo = Percentile(db, 0.20);
    const double hi = Percentile(db, 0.98);
    if (hi - lo < kAudioMinRangeDb) lo = hi - kAudioMinRangeDb;
    double prev = 0;
    for (size_t i = 0; i < rms.size(); ++i) {
        double x = 0;
        if (rms[i] >= kAudioSilenceRms) x = std::clamp((20 * std::log10(rms[i]) - lo) / (hi - lo), 0.0, 1.0);
        if (x < prev) x = kAudioRelease * prev + (1 - kAudioRelease) * x;
        prev = x;
        out[i] = x < kAudioGate ? 0 : x;
    }
    return out;
}

double FoldBpmIntoRange(double bpm) {
    if (!(bpm > 0) || !std::isfinite(bpm)) return 0;
    while (bpm < 20.0) bpm *= 2.0;
    while (bpm > 240.0) bpm /= 2.0;
    if (bpm < 20.0 || bpm > 240.0) return 0;
    return std::round(bpm);
}

double EstimateBpmFromLoudness(const std::vector<double>& loudness, int step_ms) {
    if (step_ms <= 0 || loudness.size() < 4) return 0;
    // First 10 minutes (0.7 EstimatePlaybackAudioBPM).
    const size_t max_n = size_t(10 * 60 * 1000) / size_t(step_ms);
    const size_t n = std::min(loudness.size(), max_n);
    std::vector<double> peaks_ms;
    peaks_ms.reserve(n / 4);
    constexpr double kPeak = 0.35;
    for (size_t i = 1; i + 1 < n; ++i) {
        const double v = loudness[i];
        if (v < kPeak) continue;
        if (v >= loudness[i - 1] && v > loudness[i + 1]) peaks_ms.push_back(double(i) * double(step_ms));
    }
    if (peaks_ms.size() < 2) return 0;
    std::vector<double> gaps;
    gaps.reserve(peaks_ms.size() - 1);
    for (size_t i = 1; i < peaks_ms.size(); ++i) {
        const double g = peaks_ms[i] - peaks_ms[i - 1];
        if (g >= 250.0 && g <= 3000.0) gaps.push_back(g);  // 20–240 BPM
    }
    if (gaps.empty()) return 0;
    std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
    const double period = gaps[gaps.size() / 2];
    if (!(period > 0)) return 0;
    return FoldBpmIntoRange(60000.0 / period);
}

}  // namespace samn
