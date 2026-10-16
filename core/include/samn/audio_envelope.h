#pragma once

// Loudness envelope of a sound track, 0..1 per step (SamNPlayer 0.7
// generator/audio_vibration.go). Used by the Audio Level lane under the
// curve and by "vibration from the video's sound".
//
// 0.7 piped the sound through an ffmpeg process at 4 kHz. Here the app
// decodes it itself and pushes the samples in at their own rate, so there
// is no ffmpeg dependency. The percentiles use nth_element (O(n)) instead
// of a full sort; the values are the same.

#include <cstddef>
#include <vector>

namespace samn {

// Below this RMS (~-70 dBFS) a window is silence.
constexpr double kAudioSilenceRms = 3e-4;
// Minimum dB span between "quiet" and "loud": a near-constant track must
// not turn tiny wobbles into full strength.
constexpr double kAudioMinRangeDb = 12.0;
// Normalized values below this become 0 (no idle buzz).
constexpr double kAudioGate = 0.08;
// Falling values decay with this factor (rise is immediate).
constexpr double kAudioRelease = 0.6;
// The Audio Level lane's step (0.7 audioLevelStepMs).
constexpr int kAudioLevelStepMs = 50;

// RMS per window of mono samples, fed in chunks of any size. A trailing
// partial window counts when it is at least half full.
class WindowRms {
public:
    explicit WindowRms(size_t window);
    void Push(const float* samples, size_t count);
    void Push(float sample);
    // The windows so far plus the trailing partial window (when half full).
    std::vector<double> Finish() const;
    const std::vector<double>& Windows() const { return out_; }

private:
    size_t window_;
    double sum_ = 0;
    size_t n_ = 0;
    std::vector<double> out_;
};

// Per-window RMS → 0..1: dB between the track's own quiet (20th percentile)
// and loud (98th percentile) level, silence gate, fast attack and slow
// release. Adaptive per track, so quiet and loud masters both use the full
// range.
std::vector<double> NormalizeLoudness(const std::vector<double>& rms);

}  // namespace samn
