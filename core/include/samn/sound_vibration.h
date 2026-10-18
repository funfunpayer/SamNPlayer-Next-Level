#pragma once
// Vibration from the video's sound, written into the script (0.7
// sam/audio_vibration.go and cmd/gui-wails/app_sound_vibration.go): for
// vibrators and other toys whose feel comes from vibration. The Editor's
// "Vibration from the sound" tool.
#include <cstdint>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"

namespace samn {

// Envelope resolution and written point spacing (0.7 soundVibStepMs,
// soundVibPointMs).
constexpr int64_t kSoundVibStepMs = 50;
constexpr int64_t kSoundVibPointMs = 100;

// Replaces the vibration of mapped frames with the loudness envelope
// (envelope[i] is the 0..1 level at video time i * stepMs; a frame at script
// time `at` plays at video time at + offsetMs). Suction is kept. scale
// multiplies the level (<= 0 means 1). When the sound runs longer than the
// script, frames continue at the same tick with suction 0. Inputs are not
// modified; an empty envelope or frames return the frames unchanged.
std::vector<Frame> ApplyAudioVibration(const std::vector<Frame>& frames, const std::vector<double>& envelope,
                                       int64_t stepMs, int64_t offsetMs, double scale);

// Frames sampled every kSoundVibPointMs into a 0..100 lane (vibration or
// suction); a run of equal levels keeps only its first and last point.
std::vector<Action> FramesToLane(const std::vector<Frame>& frames, bool vibration);

enum class SoundVibrationMode {
    kSound,         // vibration = the video's loudness
    kSoundContact,  // the stronger of loudness and the script's own vibration
};

// The vibration lane from the sound; `mapped` is the stroke mapped to device
// frames (ToIntensityCurve). Empty when mapped or envelope is empty.
std::vector<Action> SoundVibrationLane(const std::vector<Frame>& mapped, const std::vector<double>& envelope,
                                       int64_t offsetMs, SoundVibrationMode mode);

}  // namespace samn
