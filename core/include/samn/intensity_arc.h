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
    double min_gain = 1.25;           // a climax stands this far over the median of the rest
    int max_secondary = 2;            // climaxes besides the primary
    double secondary_share = 0.5;     // of the primary's prominence
    int64_t secondary_gap_ms = 20000;  // between any two climaxes
};

struct ArcPeak {
    int64_t t_ms = 0;       // the top of the climax
    int64_t start_ms = 0;   // the stretch around the top
    int64_t end_ms = 0;
    double prominence = 0;  // over the lows within 30 s past its top on either side, in intensity
    double confidence = 0;  // 0..1, for ranking; not a calibrated probability
};

struct IntensityArc {
    int64_t step_ms = 0;
    std::vector<double> detail;  // intensity smoothed over detail_ms, one value per step
    std::vector<double> arc;     // intensity smoothed over arc_ms
    int64_t intro_end_ms = 0;    // 0: no intro
    std::vector<ArcPeak> peaks;  // the primary climax first, then the others by rank
};

// Motion counts against the clip's busy level (its 90th percentile), the
// loudness as given (0..1). Missing, negative and NaN values count as 0; an
// input that is shorter than the clip or all 0 is missing. The clip's length
// is the motion's, or the loudness's without motion.
IntensityArc ComputeArc(const PhaseSignals& s, const ArcOptions& o = {});

// The factor the time scales shrink by for a clip of duration_ms: 1 from two
// minutes on, in proportion below.
double ArcScale(int64_t duration_ms);

}  // namespace samn
