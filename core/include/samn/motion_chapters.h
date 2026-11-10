#pragma once
// User-facing labels for stretches of script motion. Port of SamNPlayer 0.7
// motionx.Chapters (motionx/chapters.go). This is not content detection.
#include <string>
#include <vector>

#include "samn/motion_classify.h"

namespace samn {

// kind is pause | build | steady | crescendo | winddown.
struct MotionChapter {
    std::string kind;
    double start_ms = 0;
    double end_ms = 0;
};

// Starting and Accelerating in the last quarter of the segment list
// (index >= len*3/4, integer division) are crescendo; earlier ones are
// build. Adjacent chapters of the same kind become one span.
// An empty list stays empty.
std::vector<MotionChapter> MotionChaptersFromSegments(const std::vector<MotionSegment>& segs);

}  // namespace samn
