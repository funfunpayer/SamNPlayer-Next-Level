#pragma once
// Tracking between two keyframes of a label track (D-155 step 3): CSRT runs
// forward from the first key and backward from the second over the frames
// between them; samn::BlendTracks then blends the two runs and marks where
// they disagree.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/label_track.h"

namespace samn::engine {

// What a run held and did, for tests and the memory benchmark.
struct BetweenKeysStats {
    int peak_frames_held = 0;   // small frames alive at once (two windows at most; never the whole stretch)
    int frames_decoded = 0;     // frames read from the file, both runs
};

struct BetweenKeysOptions {
    int64_t step_ms = 200;    // one sample per step (a key in the track)
    int max_side = 640;       // frames are tracked at most this large
    int max_frames = 1500;    // a longer stretch is refused: add a key in between
    int window_frames = 24;   // the backward run decodes the stretch in chunks of this size, two chunks alive at once
    BetweenKeysStats* stats = nullptr;  // filled when set
    std::function<bool()> cancel;
    std::function<void(int)> progress;  // 0..100
};

struct BetweenKeysResult {
    std::vector<TrackSample> forward;
    std::vector<TrackSample> backward;
};

// Boxes in video pixels. nullopt with *error set when the video cannot be
// read, the stretch is too long, the tracker is missing or the run was
// canceled ("canceled"). Memory is bounded by two windows of window_frames, not by the length
// of the stretch (D-155 step 3 held every frame before).
std::optional<BetweenKeysResult> TrackBetweenKeys(const std::filesystem::path& video, int64_t from_ms,
                                                  const LabelBox& from_box, int64_t to_ms, const LabelBox& to_box,
                                                  const BetweenKeysOptions& o, std::string* error);

}  // namespace samn::engine
