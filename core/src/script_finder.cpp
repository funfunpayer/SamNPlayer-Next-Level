#include "samn/script_finder.h"
#include "samn/text.h"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <system_error>
#include <tuple>

namespace samn {
namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr char SEP = '\\';
#else
constexpr char SEP = '/';
#endif

bool is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

std::string ascii_lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string trim_suffix(const std::string& s, const std::string& suffix) {
    if (!suffix.empty() && ends_with(s, suffix)) return s.substr(0, s.size() - suffix.size());
    return s;
}

std::string strip_chars(const std::string& s, const char* chars) {
    auto b = s.find_first_not_of(chars);
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(chars);
    return s.substr(b, e - b + 1);
}

// Like Go's filepath.Ext: suffix from the last dot of the last element.
std::string go_ext(const std::string& path) {
    for (size_t i = path.size(); i-- > 0;) {
        char c = path[i];
        if (is_sep(c)) break;
        if (c == '.') return path.substr(i);
    }
    return "";
}

// os.path.normpath
std::string normpath(const std::string& p) {
    if (p.empty()) return ".";
    std::string prefix;
    size_t start = 0;
#ifdef _WIN32
    if (p.size() >= 2 && p[1] == ':') {
        prefix = p.substr(0, 2);
        start = 2;
    }
#endif
    size_t initial = 0;
    if (start < p.size() && is_sep(p[start])) {
        initial = 1;
        if (start + 1 < p.size() && is_sep(p[start + 1]) &&
            !(start + 2 < p.size() && is_sep(p[start + 2])))
            initial = 2;
    }
    std::vector<std::string> comps;
    size_t i = start;
    while (i <= p.size()) {
        size_t j = i;
        while (j < p.size() && !is_sep(p[j])) ++j;
        std::string comp = p.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == ".") continue;
        if (comp != ".." || (initial == 0 && comps.empty()) ||
            (!comps.empty() && comps.back() == ".."))
            comps.push_back(comp);
        else if (!comps.empty())
            comps.pop_back();
    }
    std::string out = prefix + std::string(initial, SEP);
    for (size_t k = 0; k < comps.size(); ++k) {
        if (k) out += SEP;
        out += comps[k];
    }
    if (out.empty()) return ".";
    return out;
}

std::string go_base(const std::string& path) {
    std::string p = path;
    if (p.size() > 1) {
        auto e = p.find_last_not_of("/\\");
        p = (e == std::string::npos) ? "" : p.substr(0, e + 1);
    }
    size_t i = p.size();
    while (i > 0 && !is_sep(p[i - 1])) --i;
    std::string b = p.substr(i);
    if (!b.empty()) return b;
    return path.empty() ? "." : std::string(1, SEP);
}

std::string go_dir(const std::string& path) {
    size_t i = path.size();
    while (i > 0 && !is_sep(path[i - 1])) --i;
    std::string head = path.substr(0, i);
    if (!head.empty()) {
        bool all_sep = std::all_of(head.begin(), head.end(), is_sep);
        if (!all_sep) {
            while (!head.empty() && is_sep(head.back())) head.pop_back();
        }
    }
    return head.empty() ? "." : normpath(head);
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty() || is_sep(dir.back())) return dir + name;
    return dir + SEP + name;
}

int count_sep(const std::string& s) {
    int n = 0;
    for (char c : s)
        if (c == SEP) ++n;
    return n;
}

// Sorted (name, is_dir) entries of a directory; false when unreadable.
bool list_dir(const std::string& dir, std::vector<std::pair<std::string, bool>>& out) {
    std::error_code ec;
    fs::directory_iterator it(fs::u8path(dir), ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        std::error_code e2;
        auto st = it->symlink_status(e2);
        bool is_dir = !e2 && st.type() == fs::file_type::directory;
        out.emplace_back(it->path().filename().u8string(), is_dir);
    }
    if (ec) return false;
    std::sort(out.begin(), out.end());
    return true;
}

bool has_script_ext(const std::string& ext_lower) {
    return std::find(SCRIPT_EXTS.begin(), SCRIPT_EXTS.end(), ext_lower) != SCRIPT_EXTS.end();
}

const std::set<std::string>& axis_labels() {
    static const std::set<std::string> a = {
        "surge", "sway", "twist", "roll", "pitch", "l0", "l1", "l2", "r0", "r1", "r2",
        "v0", "v1", "v2", "a0", "a1", "a2", "vib", "vibe", "vibrate", "suck", "suction",
        "lube", "valve", "stroke", "raw",
    };
    return a;
}

struct Cand {
    ScriptVariant variant;
    int rank;
};

}  // namespace

std::vector<std::string> split_folders(const std::string& s) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        std::string d = strip_chars(s.substr(pos, nl - pos), " \t\n\r\f\v");
        pos = nl + 1;
        if (d.empty() || !seen.insert(d).second) continue;
        out.push_back(d);
    }
    return out;
}

std::string normalize_title(const std::string& stem) {
    static const std::regex re_brackets(R"([\[{][^\]}]*[\]}])");
    static const std::regex re_year(R"(\((19|20)\d\d\))");
    static const std::regex re_sep(R"([\s._\-]+)");
    static const std::regex re_tags(
        R"(\b(2160p|1440p|1080p|720p|480p|4k|8k|uhd|fhd|hd|x264|x265|h264|h265|hevc|av1)"
        R"(|60fps|30fps|vr180|vr|sbs|lr|180x180|3dh)\b)",
        std::regex::ECMAScript | std::regex::icase);
    std::string s = FoldCase(stem);
    s = std::regex_replace(s, re_brackets, " ");
    s = std::regex_replace(s, re_year, " ");
    s = std::regex_replace(s, re_sep, " ");
    s = std::regex_replace(s, re_tags, " ");
    // " ".join(s.split())
    std::string out;
    size_t i = 0;
    const std::string ws = " \t\n\r\f\v";
    while (i < s.size()) {
        size_t b = s.find_first_not_of(ws, i);
        if (b == std::string::npos) break;
        size_t e = s.find_first_of(ws, b);
        if (e == std::string::npos) e = s.size();
        if (!out.empty()) out += ' ';
        out += s.substr(b, e - b);
        i = e;
    }
    return strip_chars(out, " ()");
}

std::pair<std::string, bool> variant_label(const std::string& script_stem,
                                           const std::string& video_stem) {
    if (FoldCase(script_stem) == FoldCase(video_stem)) return {"", true};
    std::string s = normalize_title(script_stem), v = normalize_title(video_stem);
    if (v.empty()) return {"", false};
    if (s == v) return {"", true};
    std::string vp = v + " ";
    if (s.compare(0, vp.size(), vp) != 0) return {"", false};
    std::string rest = strip_chars(s.substr(v.size()), " \t\n\r\f\v");
    rest = strip_chars(rest, " ()-");
    if (rest.empty()) return {"", true};
    // "Clip 2" is another clip, not a variant of "Clip"; an axis file is no
    // variant either. (Byte length, like Go's len().)
    if (strip_chars(rest, "0123456789 ").empty() || axis_labels().count(rest) ||
        rest.size() > 40)
        return {"", false};
    return {rest, true};
}

VideoScriptMatch find_script_for_video(const std::string& video_path_in,
                                       const std::vector<std::string>& folders) {
    VideoScriptMatch out;
    out.video_path = video_path_in;
    if (video_path_in.empty()) return out;
    const std::string video_path = video_path_in;
    const std::string video_dir = go_dir(video_path);
    const std::string base = go_base(video_path);
    const std::string video_stem = trim_suffix(base, go_ext(base));
    std::vector<Cand> cands;
    std::set<std::string> seen;

    auto consider = [&](const std::string& path, bool beside) {
        std::string ext = ascii_lower(go_ext(path));
        if (!has_script_ext(ext)) return;
        std::string key = FoldCase(normpath(path));
        if (seen.count(key)) return;
        std::string pbase = go_base(path);
        std::string stem = trim_suffix(pbase, go_ext(pbase));
        auto [label, ok] = variant_label(stem, video_stem);
        if (!ok) return;
        seen.insert(key);
        // Lower rank = better: beside the video, the video's own name, exact
        // spelling, then the Emotion Script before .funscript.
        int rank = 0;
        if (!beside) rank += 100;
        if (!label.empty()) rank += 50;
        if (stem != video_stem) rank += 10;
        if (ext == ".funscript") rank += 2;
        cands.push_back({ScriptVariant{path, label, beside}, rank});
    };

    std::vector<std::pair<std::string, bool>> names;
    if (!list_dir(video_dir, names)) names.clear();
    for (const auto& [name, is_dir] : names)
        if (!is_dir) consider(normpath(join_path(video_dir, name)), true);

    for (const auto& root : folders)
        walk_script_folder(root, [&](const std::string& p) { consider(p, go_dir(p) == video_dir); });

    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return std::tie(a.rank, a.variant.label, a.variant.path) <
               std::tie(b.rank, b.variant.label, b.variant.path);
    });

    // One entry per variant: a .samn and its .funscript of the same name count
    // once (the better one, already first).
    std::set<std::string> by_name;
    for (const auto& c : cands) {
        std::string k = FoldCase(trim_suffix(c.variant.path, go_ext(c.variant.path)));
        if (!by_name.insert(k).second) continue;
        out.variants.push_back(c.variant);
    }
    if (!out.variants.empty()) {
        out.script_path = out.variants[0].path;
        out.found = true;
    }
    return out;
}

void walk_script_folder(const std::string& root_in,
                        const std::function<void(const std::string&)>& fn, int max_files) {
    const std::string root = normpath(root_in);
    const int base_depth = count_sep(root);
    int files = 0;

    std::error_code ec;
    auto st = fs::symlink_status(fs::u8path(root), ec);
    if (ec) return;  // lstat failed
    const bool root_is_dir = st.type() == fs::file_type::directory;

    auto visit_file = [&](const std::string& p) {
        ++files;
        if (files > max_files) return false;  // limit reached: stop the whole walk
        if (has_script_ext(ascii_lower(go_ext(p)))) fn(p);
        return true;
    };

    std::function<bool(const std::string&)> visit_dir = [&](const std::string& p) {
        if (count_sep(p) - base_depth >= SCRIPT_FOLDER_MAX_DEPTH) return true;  // skip
        std::vector<std::pair<std::string, bool>> entries;
        if (!list_dir(p, entries)) return true;
        for (const auto& [name, is_dir] : entries) {
            std::string child = join_path(p, name);
            if (is_dir) {
                if (!visit_dir(child)) return false;
            } else if (!visit_file(child)) {
                return false;
            }
        }
        return true;
    };

    if (root_is_dir)
        visit_dir(root);
    else
        visit_file(root);
}

}  // namespace samn
