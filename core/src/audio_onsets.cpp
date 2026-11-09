#include "samn/audio_onsets.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace samn {

namespace {

constexpr double kPi = 3.14159265358979323846;
// log(1 + gain x |X|) with |X| = 1 for a full-scale sine: the compression
// reaches down to about -80 dBFS.
constexpr float kLogGain = 1000.0f;
// Frames quieter than this RMS (-60 dBFS) do not count to the flatness.
constexpr float kFrameSilenceRms = 1e-3f;
constexpr double kPowerFloor = 1e-12;
constexpr int kBands = 24;
// A frame 20 dB under the block's loud frames (90th percentile) is quiet.
constexpr float kQuietRatio = 0.1f;

int FrameSizeFor(int sample_rate) {
    const double target = 0.0464 * sample_rate;  // 512 samples at 11025 Hz
    int n = 64;
    while (n * 2 <= 8192 && std::fabs(n * 2 - target) < std::fabs(n - target)) n *= 2;
    return n;
}

}  // namespace

const char* AudioClassName(AudioClass c) {
    switch (c) {
        case AudioClass::kSilence: return "silence";
        case AudioClass::kNoise: return "noise";
        case AudioClass::kSpeechLike: return "speech-like";
        case AudioClass::kMusic: return "music";
    }
    return "silence";
}

OnsetCurve::OnsetCurve(int sample_rate)
    : sample_rate_(std::clamp(sample_rate, 4000, 96000)),
      frame_(FrameSizeFor(sample_rate_)),
      hop_(frame_ / 2),
      block_frames_(std::max(1, int(std::lround(double(sample_rate_) / double(hop_))))) {
    const auto n = std::size_t(frame_);
    ring_.assign(n, 0.0f);
    window_.resize(n);
    double wsum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        window_[i] = float(0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n)));
        wsum += window_[i];
    }
    mag_scale_ = float(2.0 / wsum);
    re_.resize(n);
    im_.resize(n);
    power_.resize(n / 2 + 1);
    // Twiddles stage by stage (1 + 2 + ... + n / 2 = n - 1), contiguous so
    // the butterflies vectorise.
    tw_re_.reserve(n - 1);
    tw_im_.reserve(n - 1);
    for (std::size_t len = 2; len <= n; len <<= 1) {
        for (std::size_t k = 0; k < len / 2; ++k) {
            const double a = -2 * kPi * double(k) / double(len);
            tw_re_.push_back(float(std::cos(a)));
            tw_im_.push_back(float(std::sin(a)));
        }
    }
    bitrev_.resize(n);
    int bits = 0;
    while ((std::size_t(1) << bits) < n) ++bits;
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (std::size_t(1) << b)) r |= std::uint32_t(1) << (bits - 1 - b);
        bitrev_[i] = r;
    }
    // Log-spaced bands from 30 Hz to Nyquist, at least one bin each: a
    // broadband hat must not outweigh a kick only because it covers more
    // linear bins.
    const double nyquist = sample_rate_ / 2.0, bin_hz = double(sample_rate_) / double(n);
    band_edges_.push_back(1);
    for (int b = 1; b <= kBands; ++b) {
        const double f = 30.0 * std::pow(nyquist / 30.0, double(b) / kBands);
        auto bin = std::uint32_t(std::lround(f / bin_hz)) + (b == kBands ? 1 : 0);
        bin = std::min<std::uint32_t>(std::uint32_t(n / 2 + 1), bin);
        if (bin > band_edges_.back()) band_edges_.push_back(bin);
    }
    prev_log_.assign(band_edges_.size() - 1, 0.0f);
    block_rms_.reserve(std::size_t(block_frames_));
    // Centred frames: half a frame of silence before the first sample, so
    // frame 0 is analysed once frame / 2 samples are in.
    ring_pos_ = n / 2;
    until_frame_ = n / 2;
}

void OnsetCurve::Push(const float* samples, std::size_t count) {
    if (finished_ || !samples) return;
    for (std::size_t i = 0; i < count; ++i) {
        const float s = samples[i];
        Feed(std::isfinite(s) ? std::clamp(s, -1.0f, 1.0f) : 0.0f);
    }
    samples_in_ += count;
}

void OnsetCurve::Feed(float sample) {
    if (ring_[ring_pos_] != 0.0f) --nonzero_;
    if (sample != 0.0f) ++nonzero_;
    ring_[ring_pos_] = sample;
    if (++ring_pos_ == ring_.size()) ring_pos_ = 0;
    if (--until_frame_ == 0) {
        until_frame_ = std::size_t(hop_);
        AnalyseFrame();
    }
}

void OnsetCurve::Finish() {
    if (finished_) return;
    // Every sample is the centre's neighbour of some frame: frames up to
    // the one centred on the last sample.
    const std::uint64_t need = samples_in_ == 0 ? 0 : (samples_in_ - 1) / std::uint64_t(hop_) + 1;
    while (frames_ < need) Feed(0.0f);
    if (!block_rms_.empty() && int(block_rms_.size()) * 2 >= block_frames_) CloseBlock();
    block_rms_.clear();
    finished_ = true;
}

void OnsetCurve::AnalyseFrame() {
    const std::size_t n = ring_.size();
    const bool silent = nonzero_ == 0;
    double sq = 0;
    if (!silent)
        for (float x : ring_) sq += double(x) * x;
    const float rms = float(std::sqrt(sq / double(n)));
    const std::size_t bins = n / 2 + 1;
    double flux = 0;
    if (silent) {
        // Digital silence: the spectrum is zero, no FFT needed.
        std::fill(prev_log_.begin(), prev_log_.end(), 0.0f);
    } else {
        // Oldest sample first: ring_[ring_pos_ ..] then ring_[.. ring_pos_).
        const std::size_t tail = n - ring_pos_;
        for (std::size_t j = 0; j < tail; ++j) re_[bitrev_[j]] = ring_[ring_pos_ + j] * window_[j];
        for (std::size_t j = tail; j < n; ++j) re_[bitrev_[j]] = ring_[j - tail] * window_[j];
        std::fill(im_.begin(), im_.end(), 0.0f);
        // Radix-2 decimation in time on split real / imaginary arrays.
        float* re = re_.data();
        float* im = im_.data();
        const float* twr = tw_re_.data();
        const float* twi = tw_im_.data();
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const std::size_t half = len / 2;
            for (std::size_t i = 0; i < n; i += len) {
                float* ar = re + i;
                float* ai = im + i;
                float* br = re + i + half;
                float* bi = im + i + half;
                for (std::size_t k = 0; k < half; ++k) {
                    const float tr = twr[k] * br[k] - twi[k] * bi[k];
                    const float ti = twr[k] * bi[k] + twi[k] * br[k];
                    br[k] = ar[k] - tr;
                    bi[k] = ai[k] - ti;
                    ar[k] += tr;
                    ai[k] += ti;
                }
            }
            twr += half;
            twi += half;
        }
        for (std::size_t k = 0; k < bins; ++k) power_[k] = re_[k] * re_[k] + im_[k] * im_[k];
        double log_sum = 0, pow_sum = 0;
        if (rms >= kFrameSilenceRms) {
            const float scale2 = mag_scale_ * mag_scale_;
            for (std::size_t k = 1; k + 1 < bins; ++k) {
                const float p = power_[k] * scale2;
                log_sum += double(std::log(p + float(kPowerFloor)));
                pow_sum += double(p);
            }
        }
        // Flux of the log band magnitudes (DC carries no onset).
        for (std::size_t b = 0; b + 1 < band_edges_.size(); ++b) {
            double p = 0;
            for (std::uint32_t k = band_edges_[b]; k < band_edges_[b + 1]; ++k) p += double(power_[k]);
            const double mag = std::sqrt(p / double(band_edges_[b + 1] - band_edges_[b])) * mag_scale_;
            const auto l = float(std::log1p(kLogGain * mag));
            flux += std::max(0.0f, l - prev_log_[b]);
            prev_log_[b] = l;
        }
        if (rms >= kFrameSilenceRms) {
            const double m = double(bins - 2);
            const double flat = std::exp(log_sum / m) / (pow_sum / m + kPowerFloor);
            block_flat_sum_ += std::clamp(flat, 0.0, 1.0);
            ++block_flat_n_;
        }
    }
    values_.push_back(std::isfinite(flux) ? float(flux) : 0.0f);
    ++frames_;
    block_rms_.push_back(rms);
    if (int(block_rms_.size()) >= block_frames_) CloseBlock();
}

void OnsetCurve::CloseBlock() {
    AudioBlock b;
    double mean_pow = 0;
    for (float r : block_rms_) mean_pow += double(r) * r;
    const double count = double(block_rms_.size());
    mean_pow /= count;
    // The block's loud level: its 90th-percentile frame (block_rms_ is
    // reordered, the values are not needed after this).
    const auto k90 = std::size_t(std::lround(0.9 * (count - 1)));
    std::nth_element(block_rms_.begin(), block_rms_.begin() + std::ptrdiff_t(k90), block_rms_.end());
    const float loud = block_rms_[k90];
    int quiet = 0;
    for (float r : block_rms_)
        if (r < kQuietRatio * loud) ++quiet;
    b.level_db = float(10 * std::log10(mean_pow + kPowerFloor));
    b.quiet_share = float(double(quiet) / count);
    b.flatness = block_flat_n_ > 0 ? float(block_flat_sum_ / block_flat_n_) : 0.0f;
    if (b.level_db < kGateSilenceDb) b.cls = AudioClass::kSilence;
    else if (b.quiet_share >= kGateBurstyShare) b.cls = AudioClass::kSpeechLike;
    else if (b.flatness > kGateNoiseFlatness) b.cls = AudioClass::kNoise;
    else b.cls = AudioClass::kMusic;
    blocks_.push_back(b);
    block_rms_.clear();
    block_flat_sum_ = 0;
    block_flat_n_ = 0;
}

std::vector<float> OnsetCurve::TakeValues() {
    std::vector<float> out;
    out.swap(values_);
    return out;
}

std::vector<AudioBlock> OnsetCurve::TakeBlocks() {
    std::vector<AudioBlock> out;
    out.swap(blocks_);
    return out;
}

std::size_t OnsetCurve::StateBytes() const {
    return sizeof(*this) + ring_.capacity() * sizeof(float) + window_.capacity() * sizeof(float) +
           (re_.capacity() + im_.capacity() + power_.capacity() + tw_re_.capacity() + tw_im_.capacity()) * sizeof(float) +
           bitrev_.capacity() * sizeof(std::uint32_t) + band_edges_.capacity() * sizeof(std::uint32_t) +
           prev_log_.capacity() * sizeof(float) +
           block_rms_.capacity() * sizeof(float);
}

std::vector<double> PickOnsets(const std::vector<float>& curve, double fps, const OnsetPickOptions& opts) {
    std::vector<double> out;
    const std::size_t n = curve.size();
    if (n < 3 || !(fps > 0) || !std::isfinite(fps)) return out;
    const auto mean_half = std::size_t(std::max(1L, std::lround(0.1 * fps)));
    const auto range_half = std::size_t(std::max(1L, std::lround(2.0 * fps)));
    const auto wait = std::max<std::int64_t>(1, std::llround(opts.wait_ms * fps / 1000.0));
    // Prefix sums for the local mean (one double per value).
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + double(curve[i]);
    // Sliding maximum over +-range_half (monotonic deque of indices).
    std::deque<std::size_t> dq;
    std::size_t next_in = 0;
    std::int64_t last = -wait - 1;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t hi = std::min(n - 1, i + range_half);
        while (next_in <= hi) {
            while (!dq.empty() && curve[dq.back()] <= curve[next_in]) dq.pop_back();
            dq.push_back(next_in++);
        }
        while (dq.front() + range_half < i) dq.pop_front();
        const double range = curve[dq.front()];
        const double v = curve[i];
        if (!(v >= opts.min_strength) || !(v > 0)) continue;
        bool peak = true;
        for (std::size_t j = (i >= 2 ? i - 2 : 0); j <= std::min(n - 1, i + 2) && peak; ++j) {
            if (j < i && !(v > curve[j])) peak = false;
            if (j > i && v < curve[j]) peak = false;
        }
        if (!peak) continue;
        const std::size_t a = i >= mean_half ? i - mean_half : 0, b = std::min(n, i + mean_half + 1);
        const double mean = (prefix[b] - prefix[a]) / double(b - a);
        if (v < mean + opts.delta * range) continue;
        if (std::int64_t(i) - last < wait) continue;
        last = std::int64_t(i);
        out.push_back(1000.0 * double(i) / fps);
    }
    return out;
}

}  // namespace samn
