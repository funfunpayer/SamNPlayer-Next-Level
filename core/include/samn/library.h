#pragma once

// The Player's Library lists (0.7 player_layout.js): recent scripts and
// videos, favorites, and the scripts that were imported, generated or
// edited. Plain path lists, newest first; the app keeps them in the settings.

#include <cstddef>
#include <string>
#include <vector>

namespace samn {

constexpr size_t kRecentMax = 8;      // 0.7 RECENT_MAX
constexpr size_t kFavoritesMax = 50;  // 0.7 FAV_MAX
constexpr size_t kScriptsMax = 50;    // 0.7 SCRIPTS_MAX

// Settings keys (0.7 kept these in the browser's localStorage).
inline constexpr const char* kRecentKey = "library.recent";
inline constexpr const char* kRecentVideosKey = "library.recent_videos";
inline constexpr const char* kFavoritesKey = "library.favorites";
inline constexpr const char* kImportedKey = "library.scripts.imported";
inline constexpr const char* kGeneratedKey = "library.scripts.generated";
inline constexpr const char* kEditedKey = "library.scripts.edited";

// path moves to the front; no duplicates (same text); at most max entries.
// An empty path changes nothing.
std::vector<std::string> PushFront(std::vector<std::string> list, const std::string& path, size_t max);

// Adds path to the front, or removes it when it is there. on tells which.
std::vector<std::string> ToggleFavorite(std::vector<std::string> list, const std::string& path, bool* on);

}  // namespace samn
