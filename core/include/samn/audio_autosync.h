#pragma once
// Player Auto sync (0.7 EstimateAudioSyncOffset / app_autosync.go): suggest a
// script offset by correlating video loudness with stroke motion speed.

#include <cstdint>
#include <string>
#include <vector>

#include "samn/audio_envelope.h"
#include "samn/funscript.h"

namespace samn {

enum class AudioSyncConfidence { None, Low, High };

struct AudioSyncSuggestion {
    int64_t offset_ms = 0;
    AudioSyncConfidence confidence = AudioSyncConfidence::None;
    double correlation = 0;
    std::string note;
};

// First `max_ms` of loudness (0..1 per step_ms) vs stroke |Δpos|/Δt, smoothed
// over 250 ms, lags ±3000 ms. Confidence: high (r≥0.30 and margin≥0.08), low
// (r≥0.12), else none. Peak gap for the runner-up is 300 ms.
AudioSyncSuggestion EstimateAudioSyncOffset(const std::vector<double>& loudness, int step_ms,
                                            const std::vector<Action>& stroke, int64_t max_ms = 600000);

}  // namespace samn
