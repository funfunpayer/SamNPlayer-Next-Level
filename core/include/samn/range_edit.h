#pragma once
// Range tools of the Editor: edit the actions inside a time range [start, end]
// (ms, inclusive). Port of SamNPlayer 0.7 funscript/project.go. Inputs are
// never modified. A swapped range (end < start) is swapped back, as in 0.7.
//
// Errors: exceptions (RangeEditError) carrying the Go error text, as marks.h.
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "samn/funscript.h"

namespace samn {

class RangeEditError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Speed above which a segment counts as too fast (Go DefaultMaxIntensity).
constexpr double DefaultMaxIntensity = 400.0;

// t snapped to the nearest frame at fps; fps <= 0, NaN or infinite returns t.
int64_t SnapMs(int64_t t, double fps);

// Removes actions with at in [start, end]. Throws RangeEditError when fewer
// than 2 actions would remain.
std::vector<Action> DeleteRange(const std::vector<Action>& actions, int64_t start, int64_t end);

// Slows too-fast segments (500 x |dpos| / dt > max_intensity) that overlap
// [start, end] by stretching their time; every later action moves right.
// max_intensity <= 0 means DefaultMaxIntensity. Throws RangeEditError with
// fewer than 2 actions.
std::vector<Action> CapSpeedRange(const std::vector<Action>& actions, int64_t start, int64_t end,
                                  double max_intensity);

// Scales positions in [start, end] around 50 by factor (result clamped to 0..100).
std::vector<Action> ScaleRangePos(const std::vector<Action>& actions, int64_t start, int64_t end,
                                  double factor);

// Same; with soft_edges the factor ramps from 1 at the range ends to `factor`
// at the midpoint.
std::vector<Action> ScaleRangePosFade(const std::vector<Action>& actions, int64_t start, int64_t end,
                                      double factor, bool soft_edges);

}  // namespace samn
