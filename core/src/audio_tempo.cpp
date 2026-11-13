#include "samn/audio_tempo.h"

#include <algorithm>
#include <cmath>

#include "samn/audio_envelope.h"

namespace samn {

namespace {

// Time constant of the running mean taken off the curve before the
// autocorrelation (a slow swell in loudness is no tempo).
constexpr double kDetrendSeconds = 0.5;
// Less than this much curve says nothing about a tempo.
constexpr double kMinTempoSeconds = 4.0;
constexpr double kBpmStep = 0.5;
// Multiples of the period used to refine it.
constexpr int kRefineMultiples = 4;
// The pulse (tatum) search: periods down to this tempo, combs of up to this
// many multiples, and the share of the best comb a shorter pulse must reach.
constexpr double kMaxPulseBpm = 320;
constexpr int kCombTerms = 6;
constexpr double kPulseShare = 0.75;
constexpr double kPulseScanStep = 0.25;  // frames
// A triple multiple of the pulse must beat the best duple one by this factor.
constexpr double kTripleHandicap = 0.9;

// Normalised autocorrelation at a fractional lag (linear interpolation).
double At(const std::vector<double>& r, double lag) {
    if (!(lag >= 0)) return 0;
    const auto i = std::size_t(lag);
    if (i + 1 >= r.size()) return i < r.size() ? r[i] : 0;
    const double f = lag - double(i);
    return r[i] + f * (r[i + 1] - r[i]);
}

bool Usable(const TempoOptions& o) {
    return o.min_bpm > 0 && o.max_bpm > o.min_bpm && std::isfinite(o.max_bpm) && o.prior_bpm > 0 &&
           o.prior_octaves > 0 && std::isfinite(o.prior_bpm) && std::isfinite(o.prior_octaves);
}

double Parabolic(double ym, double y0, double yp) {
    const double d = ym - 2 * y0 + yp;
    if (!(d < 0)) return 0;
    return std::clamp(0.5 * (ym - yp) / d, -0.5, 0.5);
}

}  // namespace

TempoAccumulator::TempoAccumulator(double fps, const TempoOptions& opts) : fps_(fps), opts_(opts) {
    if (!(fps_ > 0) || !std::isfinite(fps_)) fps_ = 0;
    if (!Usable(opts_)) opts_ = TempoOptions{};
    const double slowest = fps_ > 0 ? 60.0 * fps_ / opts_.min_bpm : 0;
    max_lag_ = std::size_t(std::ceil(slowest * kRefineMultiples)) + 3;
    mean_alpha_ = fps_ > 0 ? 1.0 / std::max(1.0, kDetrendSeconds * fps_) : 1.0;
    ring_.assign(max_lag_ + 1, 0.0f);
    acc_.assign(max_lag_ + 1, 0.0);
}

void TempoAccumulator::Push(float value) {
    if (fps_ <= 0) return;
    const double v = std::isfinite(value) && value > 0 ? double(value) : 0.0;
    const double raw = std::max(0.0, v - mean_);
    mean_ += mean_alpha_ * (v - mean_);
    // Binomial over 9 values (a standard deviation of 1.4 frames, 33 ms at
    // 11025 Hz; the delay of four values does not matter to an
    // autocorrelation).
    static constexpr double kBinomial[9] = {1, 8, 28, 56, 70, 56, 28, 8, 1};
    double d = raw * kBinomial[0];
    for (int k = 0; k < 8; ++k) d += kBinomial[k + 1] * hist_[k];
    d /= 256.0;
    for (int k = 7; k > 0; --k) hist_[k] = hist_[k - 1];
    hist_[0] = float(raw);
    ring_[pos_] = float(d);
    const std::size_t size = ring_.size();
    const auto avail = std::size_t(std::min<double>(n_, double(max_lag_)));
    // ring_[pos_ - lag] without a modulo per lag: down to 0, then from the end.
    const std::size_t direct = std::min(avail, pos_);
    for (std::size_t lag = 0; lag <= direct; ++lag) acc_[lag] += d * double(ring_[pos_ - lag]);
    for (std::size_t lag = direct + 1; lag <= avail; ++lag) acc_[lag] += d * double(ring_[pos_ + size - lag]);
    if (++pos_ == size) pos_ = 0;
    n_ += 1;
    sum_ += d;
    sum2_ += d * d;
}

void TempoAccumulator::Push(const float* values, std::size_t count) {
    if (!values) return;
    for (std::size_t i = 0; i < count; ++i) Push(values[i]);
}

std::size_t TempoAccumulator::StateBytes() const {
    return sizeof(*this) + ring_.capacity() * sizeof(float) + acc_.capacity() * sizeof(double);
}

TempoEstimate TempoAccumulator::Estimate() const {
    TempoEstimate out;
    if (fps_ <= 0 || n_ < kMinTempoSeconds * fps_) return out;
    const double mean = sum_ / n_;
    const double var = sum2_ / n_ - mean * mean;
    if (!(var > 1e-12 * std::max(1.0, mean * mean))) return out;
    // A local buffer, so Estimate never writes to the object (const and
    // safe to call from several threads at once).
    std::vector<double> r(acc_.size());
    for (std::size_t lag = 0; lag < r.size(); ++lag) {
        const double pairs = n_ - double(lag);
        r[lag] = pairs > 0 ? (acc_[lag] / pairs - mean * mean) / var : 0.0;
    }
    const auto R = [&r](double lag) { return At(r, lag); };
    // Octave folding: twice the period supports a tempo; a pulse at half or
    // a third of the period speaks for the faster one.
    const auto salience = [&R](double lag) {
        return R(lag) + 0.5 * R(2 * lag) - std::max({0.0, 0.5 * R(lag / 2), 0.6 * R(lag / 3)});
    };
    const auto prior = [this](double lag) {
        const double oct = std::log2(60.0 * fps_ / lag / opts_.prior_bpm) / opts_.prior_octaves;
        return std::exp(-0.5 * oct * oct);
    };
    const double lag_fast = 60.0 * fps_ / opts_.max_bpm, lag_slow = 60.0 * fps_ / opts_.min_bpm;
    double raw = -1e300, raw_lag = 0, r_sum = 0;
    int r_count = 0;
    for (double bpm = opts_.min_bpm; bpm <= opts_.max_bpm + 1e-9; bpm += kBpmStep) {
        const double lag = 60.0 * fps_ / bpm;
        if (lag < 1) break;
        const double s = salience(lag);
        if (s > raw) {
            raw = s;
            raw_lag = lag;
        }
        r_sum += R(lag);
        ++r_count;
    }
    // The pulse: the shortest period whose multiples explain the
    // autocorrelation (comb = mean of R at 1..6 times the period) nearly as
    // well as the best one. A tempo is then a duple (or, handicapped, a
    // triple) multiple of it: 2/5 or 4/5 of the tempo is never one.
    const double lag_pulse = std::max(2.0, 60.0 * fps_ / kMaxPulseBpm);
    const auto comb = [&](double lag) {
        const int terms = std::min(kCombTerms, int((double(r.size()) - 2) / lag));
        if (terms < 1) return -1.0;
        double s = 0;
        for (int k = 1; k <= terms; ++k) s += R(k * lag);
        return s / terms;
    };
    std::vector<double> combs;
    for (double lag = lag_pulse; lag <= lag_slow + 1e-9; lag += kPulseScanStep) combs.push_back(comb(lag));
    double comb_max = 0;
    for (double c : combs) comb_max = std::max(comb_max, c);
    if (!(comb_max > 0)) return out;
    double pulse = 0;
    for (std::size_t i = 1; i + 1 < combs.size(); ++i) {
        if (combs[i] >= kPulseShare * comb_max && combs[i] >= combs[i - 1] && combs[i] >= combs[i + 1]) {
            pulse = lag_pulse + double(i) * kPulseScanStep;
            break;
        }
    }
    if (pulse <= 0) pulse = lag_pulse + double(std::max_element(combs.begin(), combs.end()) - combs.begin()) * kPulseScanStep;
    double best = -1e300, best_lag = 0;
    for (double m = 1; pulse * m <= lag_slow + 1e-9; m *= 2) {
        for (double triple : {1.0, 3.0}) {
            const double lag = pulse * m * triple;
            if (lag < lag_fast - 1e-9 || lag > lag_slow + 1e-9) continue;
            const double s = salience(lag) * prior(lag) * (triple > 1 ? kTripleHandicap : 1.0);
            if (s > best) {
                best = s;
                best_lag = lag;
            }
        }
    }
    if (!(best > 0) || best_lag <= 0) return out;
    // Refine: the autocorrelation peak near the lag and near its multiples
    // (a least-squares period through them).
    const auto peak_near = [&r](double lag) {
        const auto lo = std::size_t(std::max(1.0, std::floor(lag - 1.5)));
        const auto hi = std::min(r.size() - 2, std::size_t(std::ceil(lag + 1.5)));
        std::size_t at = lo;
        for (std::size_t i = lo; i <= hi; ++i)
            if (r[i] > r[at]) at = i;
        if (at < 1 || at + 1 >= r.size()) return double(at);
        return double(at) + Parabolic(r[at - 1], r[at], r[at + 1]);
    };
    double num = 0, den = 0;
    double period = peak_near(best_lag);
    for (int k = 1; k <= kRefineMultiples; ++k) {
        if (double(k) * period + 2.5 >= double(r.size())) break;
        const double lk = peak_near(double(k) * period);
        num += double(k) * lk;
        den += double(k) * k;
        if (k == 1) period = lk;
    }
    if (den > 0) period = num / den;
    double bpm = 60.0 * fps_ / period;
    while (bpm > opts_.max_bpm) bpm /= 2;
    while (bpm < opts_.min_bpm) bpm *= 2;
    if (!std::isfinite(bpm) || bpm > opts_.max_bpm) return out;
    const double r_mean = r_count > 0 ? r_sum / r_count : 0;
    const double r_best = std::clamp(R(period), -1.0, 1.0);
    out.bpm = bpm;
    out.raw_bpm = raw_lag > 0 ? 60.0 * fps_ / raw_lag : 0;
    out.confidence = r_mean < 1 ? std::clamp((r_best - r_mean) / (1 - r_mean), 0.0, 1.0) : 0.0;
    return out;
}

TempoEstimate EstimateTempo(const float* curve, std::size_t count, double fps, const TempoOptions& opts) {
    TempoAccumulator acc(fps, opts);
    acc.Push(curve, count);
    return acc.Estimate();
}

TempoEstimate EstimateTempo(const std::vector<float>& curve, double fps, const TempoOptions& opts) {
    return EstimateTempo(curve.data(), curve.size(), fps, opts);
}

BeatTrack TrackBeats(const std::vector<float>& curve, double fps, double bpm, double tightness) {
    BeatTrack out;
    const std::size_t n = curve.size();
    if (n < 2 || !(fps > 0) || !std::isfinite(fps) || !(bpm > 0) || !std::isfinite(bpm)) return out;
    if (!(tightness >= 0) || !std::isfinite(tightness)) tightness = 100;
    const double period = 60.0 * fps / bpm;
    if (period < 2 || period > double(n)) return out;
    double sum = 0, sum2 = 0;
    for (float v : curve) {
        const double x = std::isfinite(v) && v > 0 ? double(v) : 0.0;
        sum += x;
        sum2 += x * x;
    }
    const double mean_all = sum / double(n);
    const double var = sum2 / double(n) - mean_all * mean_all;
    if (!(var > 0)) return out;
    const double inv_sd = 1.0 / std::sqrt(var);
    const auto value = [&](std::size_t i) {
        const float v = curve[i];
        return std::isfinite(v) && v > 0 ? double(v) * inv_sd : 0.0;
    };
    // Local score: the curve smoothed by a Gaussian of period / 32 (cut at
    // four standard deviations).
    const auto half = std::size_t(std::ceil(4 * period / 32.0));
    std::vector<double> kernel(2 * half + 1);
    for (std::size_t j = 0; j < kernel.size(); ++j) {
        const double x = (double(j) - double(half)) * 32.0 / period;
        kernel[j] = std::exp(-0.5 * x * x);
    }
    std::vector<float> local(n);
    double local_max = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0;
        const std::size_t a = i >= half ? i - half : 0, b = std::min(n - 1, i + half);
        for (std::size_t j = a; j <= b; ++j) s += kernel[j + half - i] * value(j);
        local[i] = float(s);
        local_max = std::max(local_max, s);
    }
    if (!(local_max > 0)) return out;
    // DP: best chain ending at each frame.
    std::vector<double> cum(n);
    std::vector<std::int32_t> back(n, -1);
    const auto lo = std::size_t(std::max(1.0, std::round(period / 2)));
    const auto hi = std::size_t(std::round(2 * period));
    const double thresh = 0.01 * local_max;
    std::vector<double> cost(hi + 1, 0.0);  // tightness x log(interval / period)^2
    for (std::size_t d = lo; d <= hi; ++d) {
        const double r = std::log(double(d) / period);
        cost[d] = tightness * r * r;
    }
    bool started = false;
    for (std::size_t i = 0; i < n; ++i) {
        double best = -1e300;
        std::int32_t arg = -1;
        if (i >= lo) {
            const std::size_t from = i >= hi ? i - hi : 0;
            for (std::size_t t = from; t + lo <= i; ++t) {
                const double s = cum[t] - cost[i - t];
                if (s > best) {
                    best = s;
                    arg = std::int32_t(t);
                }
            }
        }
        // Nothing before the first real onset links back.
        if (!started || arg < 0) {
            cum[i] = local[i];
            if (local[i] > thresh) started = true;
        } else {
            cum[i] = local[i] + best;
            back[i] = arg;
        }
    }
    // Last beat: the last local maximum of cum above half the median one.
    std::vector<double> maxima;
    for (std::size_t i = 1; i + 1 < n; ++i)
        if (cum[i] > cum[i - 1] && cum[i] >= cum[i + 1]) maxima.push_back(cum[i]);
    double median = 0;
    if (!maxima.empty()) {
        auto mid = maxima.begin() + std::ptrdiff_t(maxima.size() / 2);
        std::nth_element(maxima.begin(), mid, maxima.end());
        median = *mid;
    }
    std::int64_t last = -1;
    for (std::size_t i = n - 1; i >= 1; --i) {
        const bool is_max = cum[i] > cum[i - 1] && (i + 1 >= n || cum[i] >= cum[i + 1]);
        if (is_max && cum[i] >= 0.5 * median) {
            last = std::int64_t(i);
            break;
        }
    }
    if (last < 0) return out;
    std::vector<std::size_t> beats;
    for (std::int64_t i = last; i >= 0; i = back[std::size_t(i)]) beats.push_back(std::size_t(i));
    std::reverse(beats.begin(), beats.end());
    // Trim weak beats at both ends (below half the RMS of the beat scores).
    double rms = 0;
    for (std::size_t b : beats) rms += double(local[b]) * local[b];
    rms = std::sqrt(rms / double(beats.size()));
    std::size_t first = 0, end = beats.size();
    while (first < end && local[beats[first]] < 0.5 * rms) ++first;
    while (end > first && local[beats[end - 1]] < 0.5 * rms) --end;
    double on_beats = 0;
    for (std::size_t k = first; k < end; ++k) {
        const std::size_t b = beats[k];
        double m = value(b);
        if (b > 0) m = std::max(m, value(b - 1));
        if (b + 1 < n) m = std::max(m, value(b + 1));
        on_beats += m;
        out.beats_ms.push_back(1000.0 * double(b) / fps);
    }
    if (out.beats_ms.empty()) return out;
    on_beats /= double(end - first);
    out.bpm = bpm;
    out.confidence = on_beats > 0 ? std::clamp(1.0 - mean_all * inv_sd / on_beats, 0.0, 1.0) : 0.0;
    return out;
}

TempoReliability AssessTempo(const std::vector<AudioBlock>& blocks, const TempoEstimate& tempo) {
    TempoReliability r;
    if (blocks.empty()) {
        r.reason = "no sound analysed";
        return r;
    }
    // The clip's loud level: the 90th percentile of its sounding blocks.
    std::vector<float> levels;
    for (const auto& b : blocks)
        if (b.cls != AudioClass::kSilence) levels.push_back(b.level_db);
    float floor_db = float(kGateSilenceDb);
    if (!levels.empty()) {
        const auto k = std::size_t(std::lround(0.9 * double(levels.size() - 1)));
        std::nth_element(levels.begin(), levels.begin() + std::ptrdiff_t(k), levels.end());
        floor_db = std::max(floor_db, float(levels[k] - kGateRelativeSilenceDb));
    }
    int active = 0, noise = 0, speech = 0, music = 0;
    for (const auto& b : blocks) {
        if (b.cls == AudioClass::kSilence || b.level_db < floor_db) continue;
        ++active;
        if (b.cls == AudioClass::kNoise) ++noise;
        else if (b.cls == AudioClass::kSpeechLike) ++speech;
        else ++music;
    }
    r.active_share = double(active) / double(blocks.size());
    if (active > 0) {
        r.noise_share = double(noise) / active;
        r.speech_share = double(speech) / active;
        r.music_share = double(music) / active;
    }
    if (r.active_share < 0.25) {
        r.reason = "mostly silence";
    } else if (!(tempo.bpm > 0)) {
        r.reason = "no tempo found";
    } else if (r.noise_share > 0.5 && tempo.confidence < kTempoMinConfidenceStrict) {
        r.reason = "mostly steady noise";
    } else if (r.speech_share > 0.5 && tempo.confidence < kTempoMinConfidenceStrict) {
        r.reason = "speech-like sound without a clear pulse";
    } else if (tempo.confidence < kTempoMinConfidence) {
        r.reason = "weak pulse";
    } else {
        r.reliable = true;
    }
    return r;
}

PlaybackBpmGuess PickPlaybackAudioBpm(const TempoEstimate& tempo, const TempoReliability& reliability,
                                      const std::vector<double>& loudness, int step_ms) {
    PlaybackBpmGuess out;
    if (reliability.reliable && tempo.bpm > 0) {
        const double bpm = FoldBpmIntoRange(tempo.bpm);
        if (bpm > 0) {
            out.bpm = bpm;
            out.source = PlaybackBpmSource::kOnsets;
            out.confidence = tempo.confidence;
            return out;
        }
    }
    out.reason = reliability.reason;
    out.confidence = tempo.confidence;
    const double fallback = EstimateBpmFromLoudness(loudness, step_ms);
    if (fallback > 0) {
        out.bpm = fallback;
        out.source = PlaybackBpmSource::kLoudness;
    }
    return out;
}

double GapFillAudioHz(const PlaybackBpmGuess& guess) {
    if (guess.source != PlaybackBpmSource::kOnsets || !(guess.bpm > 0) || !std::isfinite(guess.bpm)) return 0;
    return guess.bpm / 60.0;
}

}  // namespace samn
