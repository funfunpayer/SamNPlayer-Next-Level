#pragma once
// Tracking between two keyframes of a label track (D-155 step 3): CSRT runs
// forward from the first key and backward from the second over the frames
// between them; samn::BlendTracks then blends the two runs and marks where
// they disagree.

#include <atomic>
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
    // The most frames alive at once over the run: the small frames of the two
    // chunk buffers plus the full-size read buffers (counted when first filled).
    // Atomic: the decode thread raises it too.
    std::atomic<int> peak_frames_held{0};
    int frames_decoded = 0;  // frames grabbed from the file, both runs, warm-up after a seek included
                             // (frames the decoder runs through inside a seek are not: use CPU time)
    int window_used = 0;     // the largest chunk size of the backward run
};

struct BetweenKeysOptions {
    int64_t step_ms = 200;    // one sample per step (a key in the track)
    int max_side = 640;       // frames are tracked at most this large
    int max_frames = 1500;    // a longer stretch is refused: add a key in between
    int window_frames = 24;   // the backward run decodes the stretch in chunks of this size, two chunks alive at once
    int max_window_frames = 96;   // a long GOP (costly seeks) grows the chunk up to this; two windows of 96 small frames are about 130 MB
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
