#pragma once

#include "samn/funscript.h"  // samn::Action (one definition for the whole core)
#include <vector>
#include <cstdint>

namespace samn {

struct BeatStrokeOptions {
    int top = 0;
    int bottom = 0;
    double maxSpeed = 0;
    double holdAfterMs = 0;
};

std::vector<Action> BeatStrokes(const std::vector<double>& beatsMs, const BeatStrokeOptions& o);

}  // namespace samn
