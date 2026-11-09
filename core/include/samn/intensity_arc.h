// The intensity arc of a clip (D-165): how busy and loud it is over time, at
// a detail and an arc scale, where the intro ends and where it peaks. From
// the picture's motion and the loudness alone: no model, no FFT, no random
// numbers, linear in the clip length. The phase rules (phase_rules.h) turn
// it into suggestions.
#pragma once

#include <cstdint>
#include <vector>

#include "samn/phase_rules.h"

namespace samn {

struct ArcOptions {
    double motion_weight = 0.6;       // intensity = motion share + loudness share
    double loudness_weight = 0.4;     // (one input alone when the other is missing)
    // The time scales below hold from 2 min on; shorter clips scale them down.
    int64_t detail_ms = 4000;         // smoothing of the detail curve: a burst of action
    int64_t arc_ms = 20000;           // smoothing of the arc: the swell of a scene
    int64_t build_up_ms = 15000;      // a rise at least this long is a build-up
    double intro_share = 0.4;         // the intro ends in the first 40 % of the clip
    int64_t steady_ms = 5000;         // the action has begun after this long above the intro
    double min_gain = 1.25;           // a climax reaches this many times the rest of the action (per input)
    int max_secondary = 2;            // climaxes besides the primary, in a clip calm outside its climaxes
    double secondary_share = 0.5;     // of the primary's strength
    int64_t secondary_gap_ms = 20000;  // between any two climaxes
};

struct ArcPeak {
    int64_t t_ms = 0;       // the top of the climax
    int64_t start_ms = 0;   // the stretch around the top
    int64_t end_ms = 0;
    double prominence = 0;  // over the lows within 30 s past its top on either side, in intensity
    double confidence = 0;  // 0..1, from the strength: a ranking value, not a probability
};

struct IntensityArc {
    int64_t step_ms = 0;
    std::vector<double> detail;  // intensity smoothed over detail_ms, one value per step
    std::vector<double> arc;     // intensity smoothed over arc_ms
    int64_t intro_end_ms = 0;    // 0: no intro
    std::vector<ArcPeak> peaks;  // the primary climax first, then the others by rank (only in a calm clip)
};

// Motion counts against the clip's busy level (its 90th percentile) after a
// running median that takes out scene cuts, the loudness as given (0..1).
// Missing, negative and NaN values count as 0; an input that is shorter than
// the clip or all 0 is missing. Empty steps (both inputs 0: black, or still
// and silent) are left out of the action's levels, the flanks and the noise
// estimate, and an input's 0 steps out of its own level. The clip's length
// is the motion's, or the loudness's without motion. A climax is a rise
// within the action after the intro, measured per input against that
// input's own level. The arc still blends the inputs 0.6 / 0.4, so a climax
// in the sound alone needs a much larger rise than one in the picture alone:
// on synthetic clips about 2x the rest of the action or more, where the
// picture is found from about 1.5x. Drift, an even action after a calm intro
// and a clip that opens at its top give none. The thresholds are fitted on
// synthetic clips only, and real clips are the test that counts; the known
// limits are in docs/benchmarks/INTENSITY_ARC_2026-11-06.md.
IntensityArc ComputeArc(const PhaseSignals& s, const ArcOptions& o = {});

// The factor the time scales shrink by for a clip of duration_ms: 1 from two
// minutes on, in proportion below.
double ArcScale(int64_t duration_ms);

}  // namespace samn
