#include "samn/update_check.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace samn {

namespace {

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Cuts to `max_len` code points (UTF-8), the last one replaced by "…".
std::string CutUtf8(const std::string& s, std::size_t max_len) {
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) starts.push_back(i);
    if (starts.size() <= max_len || max_len == 0) return s;
    return s.substr(0, starts[max_len - 1]) + "\xE2\x80\xA6";
}

}  // namespace

std::string LatestReleaseApiUrl() { return std::string("https://api.github.com/repos/") + kReleaseRepo + "/releases/latest"; }

std::string ReleasesPageUrl() { return std::string("https://github.com/") + kReleaseRepo + "/releases"; }
std::string ProjectPageUrl() { return std::string("https://github.com/") + kReleaseRepo; }

std::optional<std::array<int, 3>> ParseReleaseTag(const std::string& tag) {
    std::string t = Trim(tag);
    if (!t.empty() && t[0] == 'v') t.erase(0, 1);
    std::array<int, 3> out{};
    std::size_t part = 0, digits = 0;
    for (char c : t) {
        if (c == '.') {
            if (digits == 0 || ++part > 2) return std::nullopt;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 6) return std::nullopt;
            out[part] = out[part] * 10 + (c - '0');
        } else {
            return std::nullopt;
        }
    }
    if (part != 2 || digits == 0) return std::nullopt;
    return out;
}

bool IsNewerRelease(const std::string& current, const std::string& latest) {
    const auto lat = ParseReleaseTag(latest);
    if (!lat) return false;
    std::string cur = Trim(current);
    bool dev = false;
    if (const auto dash = cur.find('-'); dash != std::string::npos) {
        cur.erase(dash);
        dev = true;
    }
    const auto c = ParseReleaseTag(cur);
    if (!c) return true;
    if (*lat != *c) return *lat > *c;
    return dev;  // 2.0.0 is newer than 2.0.0-dev
}

std::string ReleaseNotesPreview(const std::string& body, std::size_t max_len) {
    std::istringstream in(body);
    std::string line, out;
    int kept = 0;
    while (kept < 4 && std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line.rfind("<!--", 0) == 0) continue;
        const auto start = line.find_first_not_of("#*_>` \t");
        if (start == std::string::npos) continue;
        line.erase(0, start);
        if (!out.empty()) out += " \xC2\xB7 ";
        out += line;
        ++kept;
    }
    return CutUtf8(out, max_len);
}

std::optional<ReleaseInfo> ParseLatestRelease(const std::string& json_text) {
    const auto j = nlohmann::json::parse(json_text, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    const auto str = [&](const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const auto flag = [&](const char* key) {
        const auto it = j.find(key);
        return it != j.end() && it->is_boolean() && it->get<bool>();
    };
    ReleaseInfo r;
    r.tag = Trim(str("tag_name"));
    if (!ParseReleaseTag(r.tag) || flag("draft") || flag("prerelease")) return std::nullopt;
    r.notes = str("body");
    const std::string page = str("html_url");
    const std::string prefix = ReleasesPageUrl() + "/";
    const bool ours = page.rfind(prefix, 0) == 0 && page.find_first_of(" \"'<>\\?#", prefix.size()) == std::string::npos;
    r.page_url = ours ? page : ReleasesPageUrl();
    return r;
}

}  // namespace samn
