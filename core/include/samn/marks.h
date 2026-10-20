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
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/funscript.h"

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
// indented: write like Go's json.MarshalIndent(doc, "", "  ") (0.7 SaveOMarkers)
// instead of compact json.Marshal (bookmarks, chapters).
void PatchMetadataKey(const std::filesystem::path& path, const std::string& key,
                      const std::string& encoded, bool indented = false);

// Replace the script's actions (the Editor's save), keep every other field
// and all metadata as they are, write atomically and indented. The actions
// are written sorted by time. Throws MarksError when the file cannot be read
// or written.
void PatchActions(const std::filesystem::path& path, const std::vector<Action>& actions);

// Go json.Marshal of a string: quoted, invalid UTF-8 replaced by U+FFFD,
// <, >, & and U+2028/2029 escaped, other non-ASCII kept as is.
std::string GoJsonString(const std::string& raw);

// Go json.Indent(dst, compact, "", "  ") for compact JSON text.
std::string GoIndent(const std::string& compact);

// Moving a script in time (0.7 ShiftBookmarks / ShiftChapters, for "Bake
// offset"): everything moves by delta_ms; what would start before 0 ms is
// dropped and counted, a chapter that only starts before 0 is cut at 0, an
// open end (0) stays open.
std::pair<std::vector<Bookmark>, int> ShiftBookmarks(const std::vector<Bookmark>& bookmarks, int64_t delta_ms);
std::pair<std::vector<Chapter>, int> ShiftChapters(const std::vector<Chapter>& chapters, int64_t delta_ms);

std::vector<Bookmark> LoadBookmarks(const std::filesystem::path& path);
std::vector<Chapter> LoadChapters(const std::filesystem::path& path);

// Replace metadata.bookmarks (sorted by time). An empty list is written as null.
void SaveBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks);
// Replace metadata.chapters (sorted by start); endTime omitted when 0, empty -> null.
void SaveChapters(const std::filesystem::path& path, const std::vector<Chapter>& chapters);

// The marked range of a script (0.7 heatmap drag): loop, zoom selection,
// O-marker and chapter from selection. Player state, not script data, so it
// lives beside the script in "<script>.marker.json" ({"startMs":…,"endMs":…},
// as 0.7 writes it).
struct RangeMarker {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    bool Valid() const { return end_ms > start_ms; }
    bool operator==(const RangeMarker& o) const { return start_ms == o.start_ms && end_ms == o.end_ms; }
};

std::filesystem::path MarkerPath(const std::filesystem::path& script);
// None when there is no file or it is unreadable / empty (a broken file
// must not block playing).
std::optional<RangeMarker> LoadMarker(const std::filesystem::path& script);
// An invalid range (end <= start) removes the file, as in 0.7.
void SaveMarker(const std::filesystem::path& script, RangeMarker marker);

// A chapter as a band on the timeline (0.7 chapterRanges): sorted by start;
// an open end (end <= start) runs to the next chapter's start or the script
// end; an empty name becomes "Chapter <n>"; index = position after sorting
// (picks one of five colours).
struct ChapterRange {
    std::string name;
    int64_t start_ms = 0, end_ms = 0;
    int index = 0;
};
std::vector<ChapterRange> ChapterRanges(std::vector<Chapter> chapters, int64_t total_ms);

// Add a scene marker = a chapter start at `at_ms` (0.7 addSceneMarkerAt):
// "Scene <n>" (n = chapters + 1) running to the next chapter's start (0 =
// open end when none); a chapter spanning the point is cut there. Returns
// nothing when a chapter already starts within kSceneMarkerMinGapMs.
constexpr int64_t kSceneMarkerMinGapMs = 250;
std::optional<std::vector<Chapter>> AddSceneMarker(const std::vector<Chapter>& chapters, int64_t at_ms);

}  // namespace samn
