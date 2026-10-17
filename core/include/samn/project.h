#pragma once
// Project sidecar "<name>.snp.json": the Player's session state beside a video
// or script (offset, seek position, marked range, bookmarks, chapters).
// Port of SamNPlayer 0.7 funscript/project.go. SaveProject writes the same
// bytes as Go's json.MarshalIndent(p, "", "  ").
//
// Errors are exceptions (ProjectError), as in marks.h.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "samn/marks.h"

namespace samn {

// Unreadable file / invalid JSON (Go: returned error).
class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

constexpr int ProjectVersion = 1;

// JSON keys as Go: version, videoPath, scriptPath, offsetMs, seekMs,
// loopMarker{startMs,endMs}, bookmarks, chapters, lastOpenedMs. Everything but
// version is omitted when empty / 0 / absent (Go omitempty). A chapter's
// endTime is omitted when 0, as in the funscript metadata.
struct Project {
    int version = 0;  // 0 is saved as ProjectVersion
    std::string video_path;
    std::string script_path;
    int64_t offset_ms = 0;
    int64_t seek_ms = 0;
    std::optional<RangeMarker> loop_marker;  // loopMarker; unlike LoadMarker it may be any range
    std::vector<Bookmark> bookmarks;
    std::vector<Chapter> chapters;
    int64_t last_opened_ms = 0;
};

// Sidecar path beside media_path: the last extension is replaced by
// ".snp.json" ("a/clip.mp4" -> "a/clip.snp.json"; no extension: appended).
std::filesystem::path ProjectPathFor(const std::filesystem::path& media_path);

// The JSON text SaveProject writes (indented by two spaces, no final newline).
// A version of 0 is written as ProjectVersion.
std::string ProjectToJson(const Project& p);

// Atomic write (WriteFileAtomic); a version of 0 is saved as ProjectVersion.
void SaveProject(const std::filesystem::path& path, const Project& p);

// Throws ProjectError when the file cannot be read or is not valid project
// JSON ("project: invalid JSON: ..."). Missing keys keep their defaults,
// key names match case-insensitively (Go), a version of 0 becomes ProjectVersion.
Project LoadProject(const std::filesystem::path& path);

}  // namespace samn
