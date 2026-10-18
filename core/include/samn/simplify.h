#pragma once
// Ramer-Douglas-Peucker on a (time, position) curve, port of SamNPlayer 0.7
// motionx.Simplify. The deviation is the vertical distance to the straight
// line between the segment ends in time (perpendicular distance would mix ms
// and position units). Iterative, so a long dense curve cannot overflow the
// stack.
#include <vector>

namespace samn {

struct SimplifyPoint {
    double t_ms = 0;
    double pos = 0;
};

// Indices to keep, ascending; first and last always kept. max_error in
// position units (<= 0 means 0.5).
std::vector<int> SimplifyIndices(const std::vector<SimplifyPoint>& pts, double max_error);

}  // namespace samn
