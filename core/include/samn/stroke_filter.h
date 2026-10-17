#pragma once

#include "samn/funscript.h"  // samn::Action (one definition for the whole core)
#include <vector>
#include <string>
#include <cstdint>

namespace samn {

struct StrokeFilter {
    std::string smooth;  // "", "pchip", "makima"
    int64_t stepMs = 0;
    double maxSpeed = 0;
    int64_t idleFillMs = 0;
};

// Amplitude of the gentle wave that fills long pauses (0.7 idleWaveAmp).
constexpr int kIdleWaveAmp = 12;

// "pchip" and "makima" stay, anything else turns smoothing off (0.7 NormalizeSmooth).
std::string NormalizeSmooth(const std::string& s);

bool Active(const StrokeFilter& f);

std::vector<Action> ApplyStrokeFilter(std::vector<Action> actions, const StrokeFilter& f);

}  // namespace samn
