#pragma once

// The self-check's verified repair (SC-5, docs/P3_SELF_CHECK_ENGINE.md):
// windows the verifier fails are tracked again from a fresh start found by
// Find tip, and a retry is kept only where it passes the same checks and
// still looks like the original, user-confirmed region (fail closed, GD-10).
// Where no retry passes, the window stays a gap: "lost" is not "wrong"
// (GD-19). The tip box stays the only writer (GD-01): a retry is a CSRT box
// like the first run, never a blend of sources.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/engine/self_check.h"
#include "samn/engine/track_box.h"

namespace samn::engine {

struct RepairOptions {
    TrackOptions track;        // the first run's options; record_checks and appearance_memory are forced on
    cv::Rect seed_box;         // the original box at the first run's first frame
    VerifyOptions verify;      // cuts are taken from each run
    double find_seconds = 5;   // Find tip looks this long from the failed window
    int max_candidates = 3;    // Find tip proposals tried per failed window
    double min_appearance = 0.75;  // c7 a retry window needs (the tracker's own gate is 0.55)
    std::function<bool()> cancel;
    std::function<void(int done, int total)> on_progress;  // failed windows handled
};

struct RepairedWindow {
    int first = 0, end = 0;    // frame indices of the first run
    bool repaired = false;     // false = left as a gap
    int candidate = -1;        // which Find tip proposal verified (0 = strongest)
    cv::Rect box;              // its start box
};

struct RepairResult {
    std::vector<double> positions;  // the first run with repaired windows spliced in; NaN in gaps
    std::vector<int> run_starts;    // sample indices where a spliced retry begins (its own camera frame)
    std::vector<WindowVerdict> verdicts;  // the first run's verdicts
    std::vector<RepairedWindow> windows;  // one per failed window
    int failed = 0, repaired = 0, gaps = 0;
    bool canceled = false;
};

// first: a TrackBox run of the same video with record_checks on.
// nullopt with *error only when the video cannot be read.
std::optional<RepairResult> RepairTrack(const std::filesystem::path& video, const TrackResult& first,
                                        const RepairOptions& options, std::string* error);

}  // namespace samn::engine
