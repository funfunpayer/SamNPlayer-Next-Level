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

std::optional<double> position_at(const std::vector<Action>& actions, int64_t t_ms) {
    if (actions.empty()) return std::nullopt;
    auto it = std::upper_bound(actions.begin(), actions.end(), t_ms,
                               [](int64_t t, const Action& a) { return t < a.at; });
    if (it == actions.begin()) return static_cast<double>(actions.front().pos);
    if (it == actions.end()) return static_cast<double>(actions.back().pos);
    const Action& a0 = *(it - 1);
    const Action& a1 = *it;
    if (a1.at == a0.at) return static_cast<double>(a1.pos);
    return a0.pos + static_cast<double>(static_cast<int64_t>(a1.pos - a0.pos) * (t_ms - a0.at)) /
                        static_cast<double>(a1.at - a0.at);
}

std::optional<int64_t> LoopRange::jump(int64_t video_ms) const {
    if (end_ms > start_ms && video_ms >= end_ms) return start_ms;
    return std::nullopt;
}

void Session::open_script(const std::string& path, std::vector<Action> acts,
                          const std::optional<std::string>& video) {
    script_path = path;
    actions = std::move(acts);
    video_path = video ? *video : find_matching_video(path).value_or("");
    nlohmann::json v = settings.get(offset_key(path), 0);
    int64_t ms = 0;
    if (v.is_number_integer())
        ms = v.get<int64_t>();
    else if (v.is_number_float())
        ms = static_cast<int64_t>(v.get<double>());
    offset_ms = clamp_offset(ms);
    loop.reset();
}

int64_t Session::set_offset(int64_t ms) {
    offset_ms = clamp_offset(ms);
    if (!script_path.empty()) settings.set(offset_key(script_path), offset_ms);
    return offset_ms;
}

}  // namespace samn
