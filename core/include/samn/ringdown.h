#pragma once
// Ring-down (0.7 funscript/ringdown.go): a hold should not fall from a high
// position to 0 in one tick. Appends one or two damped half-cycles after
// at_ms, then a final 0. Points already in the list are left as they are.

#include <cstdint>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// cycles clamps to 1..2 (anything below 1 becomes 1, anything above 2
// becomes 2). last_pos clamps to 0..100. Each step is 180 ms.
std::vector<Action> RingDown(std::vector<Action> actions, int64_t at_ms, int last_pos, int cycles);

}  // namespace samn
