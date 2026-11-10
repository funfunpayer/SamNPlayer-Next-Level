#pragma once
// Coarse motion states of a (time, position) curve. Port of SamNPlayer 0.7
// motionx.Classify (motionx/state.go). It says how the curve moves, not what
// is moving.
//
// SmoothRadius is the Go zero value: applyDefaults only resets a negative
// radius to 0. An empty ClassifyOptions does not smooth. AnalyzeScript is
// the call site that passes 2 (script_analysis.h).
#include <vector>

namespace samn {

enum class MotionState {
    Static,        // "static"
    Starting,      // "starting"
    Accelerating,  // "accelerating"
    Regular,       // "regular"
    Decelerating,  // "decelerating"
    Stopping,      // "stopping"
};

const char* MotionStateName(MotionState state);

struct MotionPoint {
    double t_ms = 0;
    double pos = 0;
};

// One contiguous run of a single state.
struct MotionSegment {
    MotionState state = MotionState::Static;
    double start_ms = 0;
    double end_ms = 0;
    // Average absolute speed in position units per second.
    double mean_speed = 0;
    int samples = 0;
    double Duration() const { return end_ms - start_ms; }
};

struct ClassifyOptions {
    // Speed at or below which motion counts as standstill, in position
    // units per second. <= 0 means 4.
    double static_speed = 0;
    // Relative speed change that counts as accelerating or decelerating.
    // <= 0 means 0.25.
    double change_ratio = 0;
    // Runs shorter than this merge into the neighbour they resemble.
    // <= 0 means 250.
    double min_segment_ms = 0;
    // Average the speed over +/- this many samples before classifying.
    // 0 leaves the speed raw. A negative value is treated as 0. This is
    // not defaulted to 2.
    int smooth_radius = 0;
};

// Fewer than 2 points yields an empty list. The caller's options are not
// modified (Go passes them by value).
std::vector<MotionSegment> ClassifyMotion(const std::vector<MotionPoint>& points, ClassifyOptions opt);

}  // namespace samn
