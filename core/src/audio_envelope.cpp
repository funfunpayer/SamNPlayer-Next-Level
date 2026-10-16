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

}  // namespace samn
