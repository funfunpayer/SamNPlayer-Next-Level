#pragma once
// Funscript bookmarks / chapters metadata (OFS-compatible). Port of SamNPlayer
// funscript/bookmarks.go via samn/formats/marks.py.
//
// Reads and writes metadata.bookmarks and metadata.chapters. Saving replaces
// only metadata.<key>; every other key (top level and inside metadata) is
// kept. Like the Go code, a save re-serialises the file as compact JSON with
// sorted keys at the top level and inside metadata, while untouched values
// keep their original text and key order (Go json.RawMessage).
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace samn {

// Unreadable file / invalid JSON / invalid metadata (Go: returned error).
class MarksError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Named time marker (JSON "time").
struct Bookmark {
    std::string name;
    int64_t time_ms = 0;
    bool operator==(const Bookmark& o) const { return name == o.name && time_ms == o.time_ms; }
};

// Named span; end_ms 0 means open-ended (JSON "startTime"/"endTime", endTime
// omitted when 0).
struct Chapter {
    std::string name;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    bool operator==(const Chapter& o) const {
        return name == o.name && start_ms == o.start_ms && end_ms == o.end_ms;
    }
};

// "[[H:]M:]S[.fff]" ("00:01:23.456", "1:23.5") as ms; nullopt where Go returns ok == false.
std::optional<int64_t> ParseClockMs(const std::string& s);

// Values under 1e5 with a fractional part look like seconds (OFS often uses
// 12.5); everything else stays ms. Rounds half away from zero.
int64_t NormalizeTimeToMs(double v);

// Go flexTimeMs.UnmarshalJSON on an already-parsed JSON value. null -> 0;
// number -> NormalizeTimeToMs; string -> number text or clock text. Anything
// else (and unreadable text) throws MarksError.
int64_t FlexTimeMs(const nlohmann::json& value);

// metadata as {key: raw JSON text}.
std::map<std::string, std::string> ReadMetadataMap(const std::filesystem::path& path);

// Set metadata[key] (encoded is JSON text), keep all other keys, write atomically.
void PatchMetadataKey(const std::filesystem::path& path, const std::string& key,
                      const std::string& encoded);

std::vector<Bookmark> LoadBookmarks(const std::filesystem::path& path);
std::vector<Chapter> LoadChapters(const std::filesystem::path& path);

// Replace metadata.bookmarks (sorted by time). An empty list is written as null.
void SaveBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks);
// Replace metadata.chapters (sorted by start); endTime omitted when 0, empty -> null.
void SaveChapters(const std::filesystem::path& path, const std::vector<Chapter>& chapters);

}  // namespace samn
