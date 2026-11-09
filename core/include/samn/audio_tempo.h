#pragma once

// Tempo and beats from an onset curve (samn/audio_onsets.h), D-168.
//
// EstimateTempo: autocorrelation of the onset curve (detrended, smoothed by
// a binomial kernel of 1.4 frames so a period between two frames is not
// split, accumulated value by value: it streams with a fixed state of a
// few kilobytes), octave
// folding (the evidence at twice the period counts for a tempo, a pulse at
// half or a third of the period counts against it) and a log-Gaussian prior around the
// base tempo (72 BPM, samn/base_rhythm.h). The confidence is how far the
// autocorrelation at the chosen period stands above its mean, 0..1.
//
// TrackBeats: dynamic-programming beat tracking (Ellis 2007) at a given
// tempo, a fallback beat grid where nothing better exists. Offline over the
// curve: 16 bytes per onset value (about 690 bytes per second of sound at
// 11025 Hz, 25 MB for 10 hours).
//
// AssessTempo: the gate's verdict whether a tempo can be trusted (silence,
// steady noise and speech-like sound make it unreliable).

#include <cstddef>
#include <vector>

#include "samn/audio_onsets.h"
#include "samn/base_rhythm.h"

namespace samn {

struct TempoOptions {
    double prior_bpm = kBaseBpmDefault;  // centre of the prior
    double prior_octaves = 1.0;          // its width (standard deviation)
    double min_bpm = 40;
    double max_bpm = 240;
};

struct TempoEstimate {
    double bpm = 0;         // 0 = none (too short, silent or flat)
    double confidence = 0;  // 0..1
    double raw_bpm = 0;     // the strongest folded period without the prior
};

class TempoAccumulator {
public:
    TempoAccumulator(double fps, const TempoOptions& opts = {});
    void Push(float value);
    void Push(const float* values, std::size_t count);
    TempoEstimate Estimate() const;
    std::size_t Count() const { return std::size_t(n_); }
    // Bytes of state; fixed after construction (about 6 KB at 43 fps).
    std::size_t StateBytes() const;

private:
    double R(double lag) const;  // normalised autocorrelation, interpolated

    double fps_;
    TempoOptions opts_;
    std::size_t max_lag_;
    double mean_alpha_;
    double mean_ = 0;
    float hist_[8] = {};  // the last detrended values, for the smoothing
    double n_ = 0, sum_ = 0, sum2_ = 0;
    std::size_t pos_ = 0;
    std::vector<float> ring_;    // the last max_lag_ + 1 detrended values
    std::vector<double> acc_;    // sum of d[t] * d[t - lag]
    // Filled by Estimate (mutable cache of r(lag)).
    mutable std::vector<double> r_;
};

TempoEstimate EstimateTempo(const float* curve, std::size_t count, double fps, const TempoOptions& opts = {});
TempoEstimate EstimateTempo(const std::vector<float>& curve, double fps, const TempoOptions& opts = {});

struct BeatTrack {
    std::vector<double> beats_ms;
    double bpm = 0;
    double confidence = 0;  // 0..1: onset strength on the beats against the mean
};

// Beats at about bpm through the curve. tightness weighs the log-ratio of
// a beat interval to the period (Ellis: 100). Empty for bpm <= 0 or a
// curve without any onset.
BeatTrack TrackBeats(const std::vector<float>& curve, double fps, double bpm, double tightness = 100);

// Thresholds of AssessTempo.
constexpr double kTempoMinConfidence = 0.30;
// When most sound is steady noise or speech-like, the pulse must be clear.
constexpr double kTempoMinConfidenceStrict = 0.60;

struct TempoReliability {
    bool reliable = false;
    double active_share = 0;  // blocks that are not silence
    double noise_share = 0;   // of the active blocks
    double speech_share = 0;  // of the active blocks
    double music_share = 0;   // of the active blocks
    const char* reason = "";  // why not, in English; empty when reliable
};

TempoReliability AssessTempo(const std::vector<AudioBlock>& blocks, const TempoEstimate& tempo);

}  // namespace samn
