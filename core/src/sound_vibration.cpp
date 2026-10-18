#include "samn/sound_vibration.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace samn {

std::vector<Frame> ApplyAudioVibration(const std::vector<Frame>& frames, const std::vector<double>& envelope,
                                       int64_t stepMs, int64_t offsetMs, double scale) {
    if (frames.empty() || envelope.empty() || stepMs <= 0) return frames;
    if (scale <= 0) scale = 1;
    const auto n = int64_t(envelope.size());
    auto level = [&](int64_t at) {
        const int64_t vt = at + offsetMs;
        if (vt < 0) return 0.0;
        const int64_t i = (vt + stepMs / 2) / stepMs;
        if (i >= n) return 0.0;
        return Clamp01(envelope[size_t(i)] * scale);
    };
    std::vector<Frame> out;
    out.reserve(frames.size() + envelope.size());
    for (const Frame& f : frames) out.push_back(Frame{f.at, level(f.at), f.suction});
    int64_t tick = stepMs;
    if (frames.size() >= 2) {
        const int64_t d = frames.back().at - frames[frames.size() - 2].at;
        if (d > 0) tick = d;
    }
    const int64_t audioEnd = (n - 1) * stepMs - offsetMs;
    for (int64_t t = frames.back().at + tick; t <= audioEnd; t += tick) out.push_back(Frame{t, level(t), 0});
    return out;
}

std::vector<Action> FramesToLane(const std::vector<Frame>& frames, bool vibration) {
    std::vector<Action> out;
    int64_t next = std::numeric_limits<int64_t>::min();
    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        if (f.at < next && i != frames.size() - 1) continue;
        next = f.at + kSoundVibPointMs;
        const double v = vibration ? f.vibration : f.suction;
        const int pos = int(std::lround(std::clamp(v, 0.0, 1.0) * 100));
        const size_t n = out.size();
        if (n >= 2 && out[n - 1].pos == pos && out[n - 2].pos == pos) {
            out[n - 1].at = f.at;
            continue;
        }
        out.push_back(Action{f.at, pos});
    }
    return out;
}

std::vector<Action> SoundVibrationLane(const std::vector<Frame>& mapped, const std::vector<double>& envelope,
                                       int64_t offsetMs, SoundVibrationMode mode) {
    if (mapped.empty() || envelope.empty()) return {};
    std::vector<Frame> sound = ApplyAudioVibration(mapped, envelope, kSoundVibStepMs, offsetMs, 1);
    if (mode == SoundVibrationMode::kSoundContact)
        for (size_t i = 0; i < sound.size() && i < mapped.size(); ++i)
            sound[i].vibration = std::max(sound[i].vibration, mapped[i].vibration);
    return FramesToLane(sound, true);
}

}  // namespace samn
