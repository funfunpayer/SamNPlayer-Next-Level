#pragma once
#include <vector>
#include <cstdint>

namespace samn {

struct Action {
    int64_t at;
    int pos;
};

struct BeatStrokeOptions {
    int top = 0;
    int bottom = 0;
    double maxSpeed = 0;
    double holdAfterMs = 0;
};

std::vector<Action> BeatStrokes(const std::vector<double>& beatsMs, const BeatStrokeOptions& o);

}  // namespace samn
