#include "samn/motion_chapters.h"

namespace samn {
namespace {

std::vector<MotionChapter> MergeSameKind(std::vector<MotionChapter> in) {
    if (in.empty()) return in;
    std::vector<MotionChapter> out;
    out.push_back(std::move(in.front()));
    for (size_t i = 1; i < in.size(); ++i) {
        if (in[i].kind == out.back().kind) {
            out.back().end_ms = in[i].end_ms;
            continue;
        }
        out.push_back(std::move(in[i]));
    }
    return out;
}

}  // namespace

std::vector<MotionChapter> MotionChaptersFromSegments(const std::vector<MotionSegment>& segs) {
    if (segs.empty()) return {};
    std::vector<MotionChapter> out;
    out.reserve(segs.size());
    const int n = static_cast<int>(segs.size());
    // Go: i >= len(segs)*3/4. A one-segment list has 0, so index 0 qualifies.
    const int crescendo_from = n * 3 / 4;
    for (int i = 0; i < n; ++i) {
        const MotionState state = segs[static_cast<size_t>(i)].state;
        std::string kind = "steady";
        switch (state) {
        case MotionState::Static: kind = "pause"; break;
        case MotionState::Starting:
        case MotionState::Accelerating:
            kind = i >= crescendo_from ? "crescendo" : "build";
            break;
        case MotionState::Decelerating:
        case MotionState::Stopping: kind = "winddown"; break;
        case MotionState::Regular: kind = "steady"; break;
        }
        out.push_back(MotionChapter{std::move(kind), segs[static_cast<size_t>(i)].start_ms,
                                    segs[static_cast<size_t>(i)].end_ms});
    }
    return MergeSameKind(std::move(out));
}

}  // namespace samn
