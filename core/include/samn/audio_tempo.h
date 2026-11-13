#pragma once

// Tempo and beats from an onset curve (samn/audio_onsets.h), D-168.
//
// EstimateTempo: autocorrelation of the onset curve (detrended, smoothed by
// a binomial kernel of 1.4 frames so a period between two frames is not
// split, accumulated value by value: it streams with a fixed state of a
// few kilobytes). The pulse is the shortest period whose multiples (a comb
// over 1..6 times it) explain the autocorrelation nearly as well as the
// best one; the tempo is a duple multiple of the pulse (a triple one only
// with a handicap). For subdivisions of one beat (off-beat hats, 8ths,
// 16ths) that means octaves only, never 2/5, 4/5 or 1/3 of the tempo. A
// second, polymetric layer is different: at 2/3, 3/4 or 3/2 of the tempo
// (or two tempi summed) its common period with the beat, or the layer
// itself, can win, giving 1/3, 3/2 and similar ratios; tom layers at 4/3
// and 5/4 of the tempo are worse still (bench: 36.8 % and 57.9 % right;
// the review measured 26-37 % and 21-26 % with its own generator). See
// docs/benchmarks/AUDIO_ONSETS_2026-11-08.md. Among the multiples, octave folding (twice the
// period counts for a tempo, a pulse at half or a third of it against) and
// a log-Gaussian prior around the base tempo (72 BPM, samn/base_rhythm.h)
// choose: by design a fast song (above about 130 BPM) comes out at half its
// tempo, the stroke tempo. The confidence is how far the autocorrelation at
// the chosen period stands above its mean, 0..1.
//
// TrackBeats: dynamic-programming beat tracking (Ellis 2007) at a given
// tempo, a fallback beat grid where nothing better exists. Offline over the
// curve: 16 bytes per onset value (about 690 bytes per second of sound at
// 11025 Hz, 25 MB for 10 hours).
//
// AssessTempo: the gate's verdict whether a tempo can be trusted (silence,
// steady noise and speech-like sound make it unreliable).

#include <cstddef>
#include <cstdint>
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
    // Does not change the object (safe from several threads at once; it
    // allocates about 2 KB of scratch per call).
    TempoEstimate Estimate() const;
    std::size_t Count() const { return std::size_t(n_); }
    // Bytes of state; fixed after construction (about 6 KB at 43 fps).
    std::size_t StateBytes() const;

private:
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
    double active_share = 0;  // blocks that are not silence (absolute, or 35 dB under the loud blocks)
    double noise_share = 0;   // of the active blocks
    double speech_share = 0;  // of the active blocks
    double music_share = 0;   // of the active blocks
    const char* reason = "";  // why not, in English; empty when reliable
};

TempoReliability AssessTempo(const std::vector<AudioBlock>& blocks, const TempoEstimate& tempo);

// Playback "BPM from audio": prefer EstimateTempo when AssessTempo trusts it,
// otherwise the loudness-peak fallback (EstimateBpmFromLoudness). Thresholds
// are AssessTempo's (kTempoMinConfidence 0.30, 0.60 when the active sound is
// mostly noise or speech-like, and mostly-silence at active_share < 0.25).
// bpm is folded into 20–240. source is kNone when both fail.
enum class PlaybackBpmSource : std::uint8_t { kNone, kOnsets, kLoudness };

struct PlaybackBpmGuess {
    double bpm = 0;
    PlaybackBpmSource source = PlaybackBpmSource::kNone;
    double confidence = 0;  // EstimateTempo's when source is kOnsets
    const char* reason = "";  // AssessTempo's reason when onsets are not used
};

PlaybackBpmGuess PickPlaybackAudioBpm(const TempoEstimate& tempo, const TempoReliability& reliability,
                                      const std::vector<double>& loudness, int step_ms = 50);

// Improve fill spacing: Hz from a reliable onset tempo (PickPlaybackAudioBpm
// source kOnsets), else 0 so FillGaps uses 100 ms. Playback BPM may fall back
// to loudness; gap fill does not — a noisy peak is a bad stroke grid.
double GapFillAudioHz(const PlaybackBpmGuess& guess);

}  // namespace samn
