#pragma once
// Find the script(s) that belong to a video. Exact port of samn/library/script_finder.py
// (itself a port of cmd/gui-wails/app_open_media.go). Paths are UTF-8 std::string;
// case folding is ASCII only (like the ASCII regexes).
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace samn {

inline const std::vector<std::string> VIDEO_EXTS = {
    ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".webm", ".ts", ".m2ts",
    ".flv", ".mpg", ".mpeg", ".3gp", ".ogv",
};
inline const std::vector<std::string> SCRIPT_EXTS = {".samn", ".funscript"};

// Limits for walking a script folder: deep libraries stay responsive.
constexpr int SCRIPT_FOLDER_MAX_DEPTH = 6;
constexpr int SCRIPT_FOLDER_MAX_FILES = 50000;

// One script that fits a video. label is "" for the script with the video's own
// name, else what follows it ("soft", "by someone", ...). beside: lies next to the video.
struct ScriptVariant {
    std::string path;
    std::string label;
    bool beside = false;
};

// The script found for a video (found false: none yet). script_path is the best
// one; variants lists all, best first.
struct VideoScriptMatch {
    std::string video_path;
    std::string script_path;
    bool found = false;
    std::vector<ScriptVariant> variants;
};

// One path per line, trimmed, without blanks or duplicates.
std::vector<std::string> split_folders(const std::string& s);

// Make names comparable: lower case, no brackets, years, release tags or separators.
std::string normalize_title(const std::string& stem);

// Label of script_stem against video_stem; second is false when it does not belong.
std::pair<std::string, bool> variant_label(const std::string& script_stem,
                                           const std::string& video_stem);

// Look beside the video and in the script folders for its scripts.
VideoScriptMatch find_script_for_video(const std::string& video_path,
                                       const std::vector<std::string>& folders = {});

// Call fn for every script under root, within the depth and file limits
// (max_files is a parameter so tests can lower it); unreadable parts are skipped.
void walk_script_folder(const std::string& root,
                        const std::function<void(const std::string&)>& fn,
                        int max_files = SCRIPT_FOLDER_MAX_FILES);

}  // namespace samn
