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

bool Active(const StrokeFilter& f);

std::vector<Action> ApplyStrokeFilter(std::vector<Action> actions, const StrokeFilter& f);

}  // namespace samn
