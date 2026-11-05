#include "samn/playback.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

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
    for (const char* axis : {"surge", "sway", "twist", "roll", "pitch"}) {
        std::string cand = stem + "." + axis + ".funscript";
        std::error_code ec;
        if (fs::is_regular_file(fs::u8path(cand), ec)) out.emplace_back(axis, std::move(cand));
    }
    return out;
}

}  // namespace samn
