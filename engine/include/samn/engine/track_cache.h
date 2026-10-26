#pragma once

// A cache of raw tracks (spec FR-11): changing a post-processing setting
// re-runs only the signal chain, not the decode and the tracker. The key is
// the video's content, the algorithm version and every tracking option
// (LESSONS PF-05: a cache without the version returned old results
// silently). Opt-in; the files are safe to delete at any time.

#include <filesystem>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

#include "samn/engine/track_box.h"

namespace samn::engine {

// Bump with every change to TrackBox or its parts that can change a result.
constexpr int kTrackCacheVersion = 1;

// Hex key from the video (size, modification time and the first and last
// MiB of content), the box and every option that changes the track; empty
// when the video cannot be read.
std::string TrackCacheKey(const std::filesystem::path& video, const cv::Rect& box, const TrackOptions& options);

// nullopt on a miss or an unreadable entry (a broken file is a miss).
std::optional<TrackResult> LoadTrackCache(const std::filesystem::path& dir, const std::string& key);

// Writes atomically; throws std::runtime_error naming the path on failure.
void SaveTrackCache(const std::filesystem::path& dir, const std::string& key, const TrackResult& result);

}  // namespace samn::engine
