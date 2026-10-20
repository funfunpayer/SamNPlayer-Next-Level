#include "samn/clip_prep.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace samn {

namespace {
std::string Trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::optional<double> Number(const std::string& s) {
    if (s.empty()) return std::nullopt;
    for (char c : s)
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) return std::nullopt;
    if (std::count(s.begin(), s.end(), '.') > 1) return std::nullopt;
    try {
        return std::stod(s);
    } catch (...) {
        return std::nullopt;
    }
}
}  // namespace

std::optional<double> ParseClipTime(const std::string& text) {
    const std::string s = Trim(text);
    if (s.empty()) return std::nullopt;
    std::vector<std::string> parts;
    size_t from = 0;
    for (size_t at; (at = s.find(':', from)) != std::string::npos; from = at + 1) parts.push_back(s.substr(from, at - from));
    parts.push_back(s.substr(from));
    if (parts.size() > 3) return std::nullopt;
    double total = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto v = Number(parts[i]);
        if (!v) return std::nullopt;
        // Only the last part may have a fraction; minutes and seconds stay under 60.
        if (i + 1 < parts.size() && parts[i].find('.') != std::string::npos) return std::nullopt;
        if (i > 0 && *v >= 60) return std::nullopt;
        total = total * 60 + *v;
    }
    return total;
}

std::string FormatClipSeconds(double seconds) {
    seconds = std::max(0.0, seconds);
    const double rounded = std::round(seconds);
    if (std::abs(seconds - rounded) < 1e-6) return std::to_string(int64_t(rounded));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", seconds);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

int ClipPrepMaxWidth(const std::string& preset) {
    if (preset == "960w") return 960;
    if (preset == "1080p") return 1920;
    return 1280;
}

std::optional<std::vector<std::string>> ClipPrepArgs(const ClipPrepOptions& o, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return std::nullopt;
    };
    if (Trim(o.input).empty()) return fail("no video to cut");
    if (Trim(o.output).empty()) return fail("no file to write");
    if (o.input == o.output) return fail("the cut cannot replace its own video");
    if (o.start_s < 0) return fail("the start is before the video");
    if (o.end_s <= o.start_s) return fail("the end must be after the start");
    if (o.max_width < 16) return fail("the width is too small");
    std::vector<std::string> a{"-y",
                               "-hide_banner",
                               "-loglevel",
                               "error",
                               "-nostdin",
                               "-progress",
                               "pipe:1",
                               "-ss",
                               FormatClipSeconds(o.start_s),
                               "-i",
                               o.input,
                               "-t",
                               FormatClipSeconds(o.end_s - o.start_s),
                               "-vf",
                               "scale='min(" + std::to_string(o.max_width) + ",iw)':-2:flags=lanczos",
                               "-c:v",
                               "libx264",
                               "-preset",
                               "veryfast",
                               "-crf",
                               std::to_string(o.crf > 0 ? o.crf : 20),
                               "-pix_fmt",
                               "yuv420p"};
    if (o.no_audio) {
        a.push_back("-an");
    } else {
        for (const char* s : {"-c:a", "aac", "-b:a", "128k"}) a.push_back(s);
    }
    for (const char* s : {"-movflags", "+faststart"}) a.push_back(s);
    a.push_back(o.output);
    return a;
}

std::string SuggestClipName(const std::string& source_stem, const std::string& start, const std::string& end) {
    const auto tag = [](const std::string& raw) {
        std::string out;
        for (char c : Trim(raw)) {
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') out += c;
            else if (c == ':') out += '-';
            else out += '_';
        }
        return out.empty() ? std::string("t") : out;
    };
    const std::string stem = source_stem.empty() ? std::string("clip") : source_stem;
    return stem + "_" + tag(start) + "-" + tag(end) + ".mp4";
}

std::optional<ClipMarks> ParseClipMarks(const std::string& json, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return std::nullopt;
    };
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (const std::exception&) {
        return fail("not a marks file (no JSON)");
    }
    if (!j.is_object() || !j.contains("clips") || !j["clips"].is_array()) return fail("not a marks file (no clips)");
    ClipMarks m;
    if (j.contains("source") && j["source"].is_string()) m.source = j["source"].get<std::string>();
    if (j.contains("max_width") && j["max_width"].is_number_integer()) m.max_width = j["max_width"].get<int>();
    int n = 0;
    for (const auto& c : j["clips"]) {
        ++n;
        if (!c.is_object()) return fail("clip " + std::to_string(n) + " is not an object");
        ClipMark k;
        k.name = c.value("name", "clip_" + std::to_string(n));
        const auto text = [&](const char* key) -> std::optional<double> {
            if (!c.contains(key)) return std::nullopt;
            if (c[key].is_number()) return c[key].get<double>();
            if (c[key].is_string()) return ParseClipTime(c[key].get<std::string>());
            return std::nullopt;
        };
        auto start = text("start");
        if (!start) start = text("start_sec");
        auto end = text("end");
        if (!end)
            if (const auto d = text("duration_sec"); d && start) end = *start + *d;
        if (!start || !end || *end <= *start) return fail("clip " + std::to_string(n) + " (" + k.name + ") has no valid start and end");
        k.start_s = *start;
        k.end_s = *end;
        m.clips.push_back(std::move(k));
    }
    if (m.clips.empty()) return fail("the marks file lists no clips");
    return m;
}

std::vector<Action> CutScriptWindow(const std::vector<Action>& actions, int64_t start_ms, int64_t end_ms) {
    std::vector<Action> out;
    if (end_ms <= start_ms || actions.empty()) return out;
    const auto at = [&](int64_t t) -> std::optional<int> {
        for (size_t i = 0; i + 1 < actions.size(); ++i)
            if (actions[i].at <= t && t <= actions[i + 1].at) {
                const double span = double(actions[i + 1].at - actions[i].at);
                const double f = span > 0 ? double(t - actions[i].at) / span : 0;
                return int(std::lround(actions[i].pos + f * (actions[i + 1].pos - actions[i].pos)));
            }
        return std::nullopt;
    };
    if (const auto p = at(start_ms)) out.push_back({0, *p});
    for (const auto& a : actions)
        if (a.at > start_ms && a.at < end_ms) out.push_back({a.at - start_ms, a.pos});
    if (const auto p = at(end_ms)) out.push_back({end_ms - start_ms, *p});
    // An action exactly on an edge came in through the edge point; keep one per time.
    out.erase(std::unique(out.begin(), out.end(), [](const Action& x, const Action& y) { return x.at == y.at; }), out.end());
    return out;
}

}  // namespace samn
