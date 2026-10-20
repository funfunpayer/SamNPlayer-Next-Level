#pragma once
// Which way is up (0.7 funscript/polarity.go SuggestPolarity): a hint only,
// shown after Create. FunGen and SamNPlayer often disagree on which
// direction is "up"; flipping is a sign convention, not tracking quality,
// so nothing is applied without the user.

#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

struct PolarityHint {
    bool suggest_invert = false;
    double first_half_mean = 0;
    std::string reason;  // 0.7's German reasons, in English
};

// The mean position over the first half of the timeline (not of the point
// list, which irregular spacing would bias): below 40 suggests inverting,
// above 60 reads as consistent, between says nothing.
PolarityHint SuggestPolarity(const std::vector<Action>& actions);

}  // namespace samn
