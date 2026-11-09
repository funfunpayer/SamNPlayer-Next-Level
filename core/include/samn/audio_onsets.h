#pragma once

// Onset strength of a sound track (log spectral flux) and a simple
// silence / noise / speech-like / music gate, computed frame by frame from
// mono samples at about 11 kHz (D-168).
//
// Streaming: samples go in in chunks of any size; the working state is
// fixed (under 16 KiB at 11025 Hz: one frame, its spectrum, the previous
// band magnitudes). Digital silence skips the FFT.
// The outputs grow by 4 bytes per hop (one float, about 172 bytes per
// second of sound at 11025 Hz) plus 16 bytes per gate block (one per second)
// and can be drained with TakeValues / TakeBlocks, so a long track never
// needs a whole-file buffer.
//
// Frames are centred: frame i covers samples i * hop - frame / 2 ...
// i * hop + frame / 2, so value i belongs to the time i * hop / sample_rate.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace samn {

// The analysis rate the app should decode to (a quarter of 44.1 kHz).
constexpr int kOnsetSampleRate = 11025;
// Below this block level (dBFS) a block is silence.
constexpr double kGateSilenceDb = -60.0;
// Spectral flatness (geometric / arithmetic mean of the power spectrum, 0..1)
// above which a steady block is noise; white noise is about 0.56.
constexpr double kGateNoiseFlatness = 0.30;
// Share of quiet frames (20 dB under the block's 90th-percentile frame)
// from which a block is bursty ("speech-like": speech, or sparse percussion
// without a sustained sound under it).
constexpr double kGateBurstyShare = 0.25;

enum class AudioClass : std::uint8_t { kSilence, kNoise, kSpeechLike, kMusic };
const char* AudioClassName(AudioClass c);

// One gate block (about one second of sound).
struct AudioBlock {
    float level_db = -120;  // mean power in dBFS
    float flatness = 0;     // mean spectral flatness over the non-silent frames
    float quiet_share = 0;  // share of frames 20 dB under the block's loud ones
    AudioClass cls = AudioClass::kSilence;
};

class OnsetCurve {
public:
    // Any rate from 4000 to 96000 Hz; the frame is the power of two nearest
    // to 46 ms (512 at 11025 Hz), the hop half of it.
    explicit OnsetCurve(int sample_rate = kOnsetSampleRate);

    void Push(const float* samples, std::size_t count);
    // Flushes the tail (zero padding up to the last centred frame) and the
    // last gate block when it is at least half full. Push after Finish is
    // ignored.
    void Finish();

    int SampleRate() const { return sample_rate_; }
    int FrameSize() const { return frame_; }
    int Hop() const { return hop_; }
    double FramesPerSecond() const { return double(sample_rate_) / double(hop_); }
    // Total frames produced so far (also counts drained values).
    std::uint64_t Frames() const { return frames_; }

    // Onset strength per hop, >= 0 and finite: half-wave rectified flux of
    // the log-compressed magnitudes in 24 log-spaced bands (30 Hz-Nyquist).
    const std::vector<float>& Values() const { return values_; }
    std::vector<float> TakeValues();
    const std::vector<AudioBlock>& Blocks() const { return blocks_; }
    std::vector<AudioBlock> TakeBlocks();

    // Bytes of working state, outputs excluded; fixed after construction.
    std::size_t StateBytes() const;

private:
    void Feed(float sample);
    void AnalyseFrame();
    void CloseBlock();

    int sample_rate_;
    int frame_;
    int hop_;
    int block_frames_;
    bool finished_ = false;
    std::uint64_t samples_in_ = 0;
    std::uint64_t frames_ = 0;
    std::size_t until_frame_ = 1;  // samples still needed for the next frame
    std::size_t ring_pos_ = 0;
    std::size_t nonzero_ = 0;     // non-zero samples in the ring (digital silence skips the FFT)
    std::vector<float> ring_;     // the last frame_ samples
    std::vector<float> window_;   // Hann
    std::vector<float> re_, im_;        // FFT work arrays
    std::vector<float> tw_re_, tw_im_;  // twiddles, stage by stage
    std::vector<float> power_;          // |X|^2 per bin of the last frame
    std::vector<std::uint32_t> bitrev_;
    std::vector<std::uint32_t> band_edges_;  // first bin of each band, then the end
    std::vector<float> prev_log_;             // log magnitude per band
    float mag_scale_ = 1;
    // Current gate block.
    std::vector<float> block_rms_;
    double block_flat_sum_ = 0;
    int block_flat_n_ = 0;
    std::vector<float> values_;
    std::vector<AudioBlock> blocks_;
};

struct OnsetPickOptions {
    // A peak must beat the local mean (+-100 ms) by delta x the local range
    // (the largest value within +-2 s).
    double delta = 0.20;
    // And be at least this strong (a steady tone leaves rounding noise).
    double min_strength = 1.0;
    // At least this long between two onsets.
    int wait_ms = 70;
};

// Onset times in ms from an onset curve (fps values per second): local
// maxima over +-2 frames that pass the adaptive threshold. Linear in the
// curve length; the extra memory is one double per value.
std::vector<double> PickOnsets(const std::vector<float>& curve, double fps, const OnsetPickOptions& opts = {});

}  // namespace samn
