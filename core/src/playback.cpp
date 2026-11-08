#include "samn/playback.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "samn/fileio.h"
#include "samn/funscript.h"
#include "samn/script_finder.h"

namespace samn {
namespace fs = std::filesystem;

std::string offset_key(const std::string& script_path) { return "playback.offset." + script_path; }

int64_t clamp_offset(int64_t ms) {
    return std::max(-OFFSET_LIMIT_MS, std::min(OFFSET_LIMIT_MS, ms));
}

int64_t script_time(int64_t video_ms, int64_t offset_ms) {
    return std::max<int64_t>(0, video_ms - offset_ms);
}

namespace {
// os.path.splitext: the stem of a path without its extension.
std::string stem_of(const std::string& p) {
    size_t sep = p.find_last_of(
#ifdef _WIN32
        "/\\"
#else
        "/"
#endif
    );
    size_t dot = p.rfind('.');
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return p;
    for (size_t i = (sep == std::string::npos ? 0 : sep + 1); i < dot; ++i)
        if (p[i] != '.') return p.substr(0, dot);
    return p;
}
}  // namespace

std::optional<std::string> find_matching_video(const std::string& script_path) {
    const std::string stem = stem_of(script_path);
    for (const auto& ext : VIDEO_EXTS) {
        std::string cand = stem + ext;
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(cand), ec)) return cand;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> companion_axis_files(const std::string& script_path) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string stem = stem_of(script_path);
    for (const char* axis : kCompanionAxes) {
        std::string cand = stem + "." + axis + ".funscript";
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(cand), ec)) out.emplace_back(axis, std::move(cand));
    }
    return out;
}

bool is_companion_axis(const std::string& axis) {
    for (const char* name : kCompanionAxes)
        if (axis == name) return true;
    return false;
}

std::string companion_axis_path(const std::string& script_path, const std::string& axis) {
    return stem_of(script_path) + "." + axis + ".funscript";
}

std::optional<std::string> create_companion_axis(const std::string& script_path, const std::string& axis,
                                                 int64_t duration_ms, std::string* error) {
    auto fail = [&](const char* msg) -> std::optional<std::string> {
        if (error) *error = msg;
        return std::nullopt;
    };
    if (script_path.empty()) return fail("no script path");
    if (!is_companion_axis(axis)) return fail("unknown axis");
    const std::string path = companion_axis_path(script_path, axis);
    std::error_code ec;
    if (fs::exists(fs::u8path(path), ec)) return fail("axis file already exists");
    const int64_t end = std::max<int64_t>(1, duration_ms);
    Script script;
    script.actions = {{0, 50}, {end, 50}};
    try {
        WriteFileAtomic(fs::u8path(path), Write(script));
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
    return path;
}

}  // namespace samn
