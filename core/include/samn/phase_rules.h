// Phase suggestions without a model (D-157): rules over what the clip shows
// and sounds like. Every result is a suggestion (confirmed = false, source
// "rules"); nothing is applied until the user confirms it.
#pragma once

#include <cstdint>
#include <vector>

#include "samn/label_track.h"

namespace samn {

// One value per step through the clip, each 0..1. Loudness and tempo may be
// empty (no sound, no script); then the rules use the picture alone.
struct PhaseSignals {
    int64_t step_ms = 500;
    std::vector<double> luma;      // mean brightness of the frame
    std::vector<double> motion;    // mean change to the previous frame
    std::vector<double> loudness;  // sound level
    std::vector<double> tempo;     // stroke speed from a script, normalised
};

struct PhaseRuleOptions {
    double dark_luma = 0.06;        // darker than this is an empty picture
    double still_motion = 0.004;    // calmer than this is a still picture
    int64_t min_empty_ms = 2000;
    int64_t min_intro_ms = 5000;
    int64_t steady_ms = 5000;       // the action has begun after this long
    double active_share = 0.35;     // of the clip's busy level (90th percentile)
    int64_t climax_min_ms = 8000;
    int64_t climax_max_ms = 45000;
    double climax_gain = 1.25;      // the peak must stand out over the median
    int64_t min_cooldown_ms = 5000;
};

// The suggested phases, sorted and not overlapping: empty stretches, the
// intro before the first steady action, the climax where the action and the
// sound peak in the second half, and the cool-down after it.
std::vector<LabelPhase> SuggestPhases(const PhaseSignals& s, const PhaseRuleOptions& o = {});

// Adds suggestions to a project without touching what the user set: only
// stretches no phase covers yet are filled. Returns how many were added.
int ApplyPhaseSuggestions(LabelProject& p, const std::vector<LabelPhase>& suggestions);

}  // namespace samn
