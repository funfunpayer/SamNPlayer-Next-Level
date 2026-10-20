#include <system_error>
#include "samn/marks.h"
#include "samn/fileio.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <regex>
#include <sstream>


namespace samn {

namespace {

using RawMap = std::map<std::string, std::string>;
using nlohmann::json;
using nlohmann::ordered_json;

// ---------------------------------------------------------------- time parsing

std::optional<double> ParseFloat(const std::string& s) {
    // Go strconv.ParseFloat decimal forms; no surrounding spaces, no underscores.
    static const std::regex float_re(R"([+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)",
                                     std::regex::ECMAScript | std::regex::optimize);
    static const std::regex special_re(R"(([+-]?)(inf|infinity)|nan)",
                                       std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
    if (std::regex_match(s, float_re)) {
        const char* b = s.data();
        const char* e = b + s.size();
        if (*b == '+') ++b;  // from_chars does not accept a leading '+'
        double v = 0;
        auto r = std::from_chars(b, e, v);
        if (r.ec == std::errc()) return v;
        // out of range: overflow is an error (Go), underflow rounds to 0/denormal.
        double sv = std::strtod(s.c_str(), nullptr);
        if (std::isinf(sv)) return std::nullopt;
        return sv;
    }
    if (std::regex_match(s, special_re)) {
        std::string l = s;
        for (auto& c : l)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (l.find("nan") != std::string::npos) return std::nan("");
        double inf = std::numeric_limits<double>::infinity();
        return l[0] == '-' ? -inf : inf;
    }
    return std::nullopt;
}

int64_t RoundHalfAway(double x) {
    double r = std::round(x);
    if (r >= 9.2e18) return INT64_MAX;
    if (r <= -9.2e18) return INT64_MIN;
    return static_cast<int64_t>(r);
}

std::string PyStrip(const std::string& s) {
    auto ws = [](unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f); };
    size_t b = 0, e = s.size();
    while (b < e && ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------- JSON text helpers

bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Replace invalid UTF-8 by U+FFFD (like Python's decode(errors="replace")).
std::string SanitizeUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const size_t n = in.size();
    size_t i = 0;
    auto cont = [&](size_t k, unsigned lo, unsigned hi) {
        if (k >= n) return false;
        unsigned c = static_cast<unsigned char>(in[k]);
        return c >= lo && c <= hi;
    };
    static const char repl[] = "\xEF\xBF\xBD";
    while (i < n) {
        unsigned c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            out += in[i++];
            continue;
        }
        size_t len = 0;
        unsigned lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
        } else {
            out += repl;
            ++i;
            continue;
        }
        size_t k = i + 1;
        bool ok = true;
        for (size_t part = 1; part < len; ++part) {
            if (cont(k, part == 1 ? lo : 0x80, part == 1 ? hi : 0xBF)) {
                ++k;
            } else {
                ok = false;
                break;
            }
        }
        if (ok) {
            out.append(in, i, len);
        } else {
            out += repl;
        }
        i = k;
    }
    return out;
}

// Go json.Marshal of a string (non-ASCII kept, <>& and U+2028/2029 escaped).
void AppendHtmlEscaped(std::string& out, const std::string& s, size_t& i) {
    char c = s[i];
    if (c == '<') {
        out += "\\u003c";
    } else if (c == '>') {
        out += "\\u003e";
    } else if (c == '&') {
        out += "\\u0026";
    } else if (c == '\xE2' && i + 2 < s.size() && s[i + 1] == '\x80' && (s[i + 2] == '\xA8' || s[i + 2] == '\xA9')) {
        out += s[i + 2] == '\xA8' ? "\\u2028" : "\\u2029";
        i += 2;
    } else {
        out += c;
    }
}

std::string GoString(const std::string& raw) {
    std::string s = SanitizeUtf8(raw);
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    AppendHtmlEscaped(out, s, i);
                }
        }
    }
    out += '"';
    return out;
}

// Index just past the string literal starting at text[i] == '"'.
size_t StringEnd(const std::string& text, size_t i) {
    size_t j = i + 1;
    while (j < text.size() && text[j] != '"') j += (text[j] == '\\') ? 2 : 1;
    return j + 1;
}

// Go compacts a RawMessage when marshalling: drop whitespace outside strings
// and HTML-escape inside them; numbers/key order stay untouched.
std::string Compact(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '"') {
            size_t e = std::min(StringEnd(raw, i), raw.size());
            for (size_t k = i; k < e; ++k) AppendHtmlEscaped(out, raw, k);
            i = e;
        } else {
            if (!IsWs(raw[i])) out += raw[i];
            ++i;
        }
    }
    return out;
}

// Index just past the JSON value starting at text[i] (text is valid JSON).
size_t ValueEnd(const std::string& text, size_t i) {
    char c = text[i];
    if (c == '"') return StringEnd(text, i);
    if (c == '{' || c == '[') {
        int depth = 0;
        while (i < text.size()) {
            char d = text[i];
            if (d == '"') {
                i = StringEnd(text, i);
                continue;
            }
            if (d == '{' || d == '[') ++depth;
            if (d == '}' || d == ']') {
                if (--depth == 0) return i + 1;
            }
            ++i;
        }
        return i;
    }
    while (i < text.size() && !IsWs(text[i]) && text[i] != ',' && text[i] != '}' && text[i] != ']') ++i;
    return i;
}

// Split a *valid* JSON object into {key: raw value text} (last duplicate wins,
// like Go's map[string]RawMessage).
RawMap SplitObject(const std::string& text) {
    RawMap out;
    size_t i = text.find('{') + 1;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && IsWs(text[i])) ++i;
        if (i >= n || text[i] == '}') break;
        if (text[i] == ',') {
            ++i;
            continue;
        }
        size_t ke = StringEnd(text, i);
        std::string key = json::parse(text.substr(i, ke - i)).get<std::string>();
        i = ke;
        while (i < n && (IsWs(text[i]) || text[i] == ':')) ++i;
        size_t ve = ValueEnd(text, i);
        out[key] = text.substr(i, ve - i);
        i = ve;
    }
    return out;
}

// Go json.Marshal of map[string]RawMessage (sorted keys, compact).
std::string MarshalObject(const RawMap& obj) {
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : obj) {  // std::map: byte-wise sorted == code point order for UTF-8
        if (!first) out += ',';
        first = false;
        out += GoString(k) + ":" + Compact(v);
    }
    return out + "}";
}

// Strict parse (nlohmann rejects NaN/Infinity constants); throws json::exception.
json Loads(const std::string& text) { return json::parse(text); }

struct Doc {
    bool is_null = false;
    RawMap fields;
};

Doc ReadDoc(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::error_code ec;
    if (!f || std::filesystem::is_directory(path, ec)) {
        int e = errno;
        throw MarksError("funscript: cannot read the file: " +
                         (e ? std::generic_category().message(e) : std::string("access denied")) + ": " + path.string());
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad()) throw MarksError("funscript: cannot read the file: " + path.string());
    std::string text = SanitizeUtf8(data);
    json parsed;
    try {
        parsed = Loads(text);
    } catch (const json::exception& exc) {
        throw MarksError(std::string("funscript: invalid JSON: ") + exc.what());
    }
    Doc d;
    if (parsed.is_null()) {  // Go: nil map
        d.is_null = true;
        return d;
    }
    if (!parsed.is_object()) throw MarksError("funscript: invalid JSON: not an object");
    d.fields = SplitObject(text);
    return d;
}

RawMap MetadataOf(const Doc& doc) {
    auto it = doc.fields.find("metadata");
    if (doc.is_null || it == doc.fields.end() || it->second == "null") return {};
    try {
        if (!Loads(it->second).is_object()) throw std::invalid_argument("not an object");
    } catch (const json::exception& exc) {
        throw MarksError(std::string("funscript: invalid metadata: ") + exc.what());
    } catch (const std::invalid_argument& exc) {
        throw MarksError(std::string("funscript: invalid metadata: ") + exc.what());
    }
    return SplitObject(it->second);
}


// ---------------------------------------------------------------- wire decoding

std::string ReprForError(const json& v) {
    if (v.is_string()) return "'" + v.get<std::string>() + "'";
    return v.dump();
}

std::string AsciiLower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Go struct decoding: null -> zero value, object -> case-insensitive field
// match (later keys override), anything else is an error.
std::map<std::string, ordered_json> Fields(const ordered_json& item,
                                           const std::map<std::string, std::string>& names) {
    std::map<std::string, ordered_json> out;
    if (item.is_null()) return out;
    if (!item.is_object()) throw std::invalid_argument("not an object");
    for (auto it = item.begin(); it != item.end(); ++it) {
        auto n = names.find(AsciiLower(it.key()));
        if (n != names.end()) out[n->second] = it.value();
    }
    return out;
}

std::string NameOf(const std::map<std::string, ordered_json>& f) {
    auto it = f.find("name");
    if (it == f.end() || it->second.is_null()) return "";
    if (!it->second.is_string()) throw std::invalid_argument("name is not text");
    return it->second.get<std::string>();
}

int64_t TimeOf(const std::map<std::string, ordered_json>& f, const char* key) {
    auto it = f.find(key);
    if (it == f.end()) return 0;
    return FlexTimeMs(json(it->second));
}

ordered_json LoadArray(const std::string& raw, const std::string& what) {
    try {
        ordered_json v = ordered_json::parse(raw);
        if (!v.is_array()) throw std::invalid_argument("not an array");
        return v;
    } catch (const json::exception& exc) {
        throw MarksError("funscript: " + what + " invalid: " + exc.what());
    } catch (const std::invalid_argument& exc) {
        throw MarksError("funscript: " + what + " invalid: " + exc.what());
    }
}

template <class T, class F>
std::vector<T> LoadMarks(const std::filesystem::path& path, const char* key, F&& decode) {
    RawMap meta = ReadMetadataMap(path);
    auto it = meta.find(key);
    if (it == meta.end() || it->second == "null") return {};
    std::vector<T> out;
    try {
        for (const auto& item : LoadArray(it->second, key)) out.push_back(decode(item));
    } catch (const MarksError& exc) {
        if (std::strncmp(exc.what(), "funscript:", 10) == 0) throw;
        throw MarksError(std::string("funscript: ") + key + " invalid: " + exc.what());
    } catch (const std::invalid_argument& exc) {
        throw MarksError(std::string("funscript: ") + key + " invalid: " + exc.what());
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------- public API

std::optional<int64_t> ParseClockMs(const std::string& s) {
    std::vector<std::string> parts;
    {
        std::string t = PyStrip(s);
        size_t start = 0;
        for (;;) {
            size_t p = t.find(':', start);
            if (p == std::string::npos) {
                parts.push_back(t.substr(start));
                break;
            }
            parts.push_back(t.substr(start, p - start));
            start = p + 1;
        }
    }
    if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
    double total = 0.0;
    for (size_t i = 0; i < parts.size(); ++i) {
        auto v = ParseFloat(parts[i]);
        if (!v || *v < 0 || std::isnan(*v) || std::isinf(*v)) return std::nullopt;
        if (i + 1 < parts.size() && *v != std::trunc(*v)) return std::nullopt;  // only seconds may have a fraction
        total = total * 60 + *v;
    }
    return RoundHalfAway(total * 1000);
}

int64_t NormalizeTimeToMs(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    double a = std::fabs(v);
    double frac = a - std::trunc(a);
    if (a < 100000 && frac > 1e-9) return RoundHalfAway(v * 1000);
    return RoundHalfAway(v);
}

int64_t FlexTimeMs(const json& value) {
    if (value.is_null()) return 0;
    if (value.is_string()) {
        const std::string s = value.get<std::string>();
        auto v = ParseFloat(s);
        if (!v) {
            auto ms = ParseClockMs(s);
            if (!ms) throw MarksError("invalid time: " + ReprForError(value));
            return *ms;
        }
        return NormalizeTimeToMs(*v);
    }
    if (!value.is_number()) throw MarksError("invalid time: " + ReprForError(value));
    double f = value.get<double>();
    if (std::isinf(f)) throw MarksError("time out of range: " + ReprForError(value));
    return NormalizeTimeToMs(f);
}

RawMap ReadMetadataMap(const std::filesystem::path& path) { return MetadataOf(ReadDoc(path)); }

std::string GoJsonString(const std::string& raw) { return GoString(raw); }

std::string GoIndent(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 2);
    int depth = 0;
    bool in_str = false, esc = false, need_indent = false;
    auto newline = [&](int d) {
        out += '\n';
        out.append(static_cast<size_t>(std::max(0, d)) * 2, ' ');
    };
    for (char c : in) {
        if (in_str) {
            out += c;
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (need_indent && c != ']' && c != '}') {
            need_indent = false;
            newline(depth);
        }
        switch (c) {
            case '"':
                out += c;
                in_str = true;
                break;
            case '{':
            case '[':
                out += c;
                ++depth;
                need_indent = true;
                break;
            case ',':
                out += c;
                newline(depth);
                break;
            case ':':
                out += ": ";
                break;
            case '}':
            case ']':
                if (need_indent) need_indent = false;  // empty: stays {} / []
                else newline(depth - 1);
                --depth;
                out += c;
                break;
            default:
                out += c;
        }
    }
    return out;
}

void PatchMetadataKey(const std::filesystem::path& path, const std::string& key, const std::string& encoded,
                      bool indented) {
    Doc doc = ReadDoc(path);
    if (doc.is_null) throw MarksError("funscript: invalid JSON: not an object");
    RawMap meta = MetadataOf(doc);
    meta[key] = encoded;
    doc.fields["metadata"] = MarshalObject(meta);
    const std::string compact = MarshalObject(doc.fields);
    const std::string out = indented ? GoIndent(compact) : compact;
    try {
        WriteFileAtomic(path, out);
    } catch (const std::exception& exc) {
        throw MarksError(std::string("funscript: cannot write the file: ") + exc.what());
    }
}

void PatchActions(const std::filesystem::path& path, const std::vector<Action>& actions) {
    Doc doc = ReadDoc(path);
    if (doc.is_null) throw MarksError("funscript: invalid JSON: not an object");
    std::vector<Action> sorted = actions;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
    std::string encoded;
    encoded.reserve(sorted.size() * 24 + 2);
    encoded += '[';
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i) encoded += ',';
        encoded += "{\"at\":";
        encoded += std::to_string(sorted[i].at);
        encoded += ",\"pos\":";
        encoded += std::to_string(sorted[i].pos);
        encoded += '}';
    }
    encoded += ']';
    doc.fields["actions"] = encoded;
    try {
        WriteFileAtomic(path, GoIndent(MarshalObject(doc.fields)));
    } catch (const std::exception& exc) {
        throw MarksError(std::string("funscript: cannot write the file: ") + exc.what());
    }
}

std::vector<Bookmark> LoadBookmarks(const std::filesystem::path& path) {
    static const std::map<std::string, std::string> names{{"name", "name"}, {"time", "time"}};
    return LoadMarks<Bookmark>(path, "bookmarks", [&](const ordered_json& item) {
        auto f = Fields(item, names);
        Bookmark b;
        b.name = NameOf(f);
        b.time_ms = TimeOf(f, "time");
        return b;
    });
}

std::vector<Chapter> LoadChapters(const std::filesystem::path& path) {
    static const std::map<std::string, std::string> names{
        {"name", "name"}, {"starttime", "start"}, {"endtime", "end"}};
    return LoadMarks<Chapter>(path, "chapters", [&](const ordered_json& item) {
        auto f = Fields(item, names);
        Chapter c;
        c.name = NameOf(f);
        c.start_ms = TimeOf(f, "start");
        c.end_ms = TimeOf(f, "end");
        return c;
    });
}

void SaveBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks) {
    std::vector<Bookmark> ordered = bookmarks;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Bookmark& a, const Bookmark& b) { return a.time_ms < b.time_ms; });
    std::string encoded;
    if (ordered.empty()) {
        encoded = "null";
    } else {
        encoded = "[";
        for (size_t i = 0; i < ordered.size(); ++i) {
            if (i) encoded += ',';
            encoded += "{\"name\":" + GoString(ordered[i].name) + ",\"time\":" +
                       std::to_string(ordered[i].time_ms) + "}";
        }
        encoded += "]";
    }
    PatchMetadataKey(path, "bookmarks", encoded);
}

void SaveChapters(const std::filesystem::path& path, const std::vector<Chapter>& chapters) {
    std::vector<Chapter> ordered = chapters;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Chapter& a, const Chapter& b) { return a.start_ms < b.start_ms; });
    std::string encoded;
    if (ordered.empty()) {
        encoded = "null";
    } else {
        encoded = "[";
        for (size_t i = 0; i < ordered.size(); ++i) {
            if (i) encoded += ',';
            encoded += "{\"name\":" + GoString(ordered[i].name) + ",\"startTime\":" +
                       std::to_string(ordered[i].start_ms);
            if (ordered[i].end_ms) encoded += ",\"endTime\":" + std::to_string(ordered[i].end_ms);
            encoded += "}";
        }
        encoded += "]";
    }
    PatchMetadataKey(path, "chapters", encoded);
}

std::filesystem::path MarkerPath(const std::filesystem::path& script) {
    std::filesystem::path p = script;
    p += ".marker.json";
    return p;
}

std::optional<RangeMarker> LoadMarker(const std::filesystem::path& script) {
    std::ifstream in(MarkerPath(script), std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const json j = json::parse(in);
        RangeMarker m{j.value("startMs", int64_t{0}), j.value("endMs", int64_t{0})};
        if (!m.Valid()) return std::nullopt;
        return m;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

void SaveMarker(const std::filesystem::path& script, RangeMarker marker) {
    const auto path = MarkerPath(script);
    if (!marker.Valid()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) throw std::runtime_error("cannot remove " + path.string() + ": " + ec.message());
        return;
    }
    // Same bytes as Go's json.Marshal(Marker{...}).
    WriteFileAtomic(path, "{\"startMs\":" + std::to_string(marker.start_ms) + ",\"endMs\":" +
                              std::to_string(marker.end_ms) + "}");
}

std::vector<ChapterRange> ChapterRanges(std::vector<Chapter> chapters, int64_t total_ms) {
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const Chapter& a, const Chapter& b) { return a.start_ms < b.start_ms; });
    std::vector<ChapterRange> out;
    out.reserve(chapters.size());
    for (size_t i = 0; i < chapters.size(); ++i) {
        const Chapter& c = chapters[i];
        const int64_t next = i + 1 < chapters.size() ? chapters[i + 1].start_ms : total_ms;
        const int64_t end = c.end_ms > c.start_ms ? c.end_ms : next;
        out.push_back({c.name.empty() ? "Chapter " + std::to_string(i + 1) : c.name, c.start_ms,
                       std::max(c.start_ms, end), int(i)});
    }
    return out;
}

std::optional<std::vector<Chapter>> AddSceneMarker(const std::vector<Chapter>& chapters, int64_t at_ms) {
    const int64_t t = std::max<int64_t>(0, at_ms);
    int64_t next_start = 0;
    for (const auto& c : chapters) {
        if (std::llabs(c.start_ms - t) < kSceneMarkerMinGapMs) return std::nullopt;
        if (c.start_ms > t && (next_start == 0 || c.start_ms < next_start)) next_start = c.start_ms;
    }
    std::vector<Chapter> next = chapters;
    for (auto& c : next)
        if (c.start_ms < t && (c.end_ms == 0 || c.end_ms > t)) c.end_ms = t;
    next.push_back({"Scene " + std::to_string(chapters.size() + 1), t, next_start});
    std::stable_sort(next.begin(), next.end(), [](const Chapter& a, const Chapter& b) { return a.start_ms < b.start_ms; });
    return next;
}

std::pair<std::vector<Bookmark>, int> ShiftBookmarks(const std::vector<Bookmark>& bookmarks, int64_t delta_ms) {
    std::vector<Bookmark> out;
    int dropped = 0;
    for (const auto& b : bookmarks) {
        if (b.time_ms + delta_ms < 0) {
            ++dropped;
            continue;
        }
        out.push_back({b.name, b.time_ms + delta_ms});
    }
    return {out, dropped};
}

std::pair<std::vector<Chapter>, int> ShiftChapters(const std::vector<Chapter>& chapters, int64_t delta_ms) {
    std::vector<Chapter> out;
    int dropped = 0;
    for (const auto& c : chapters) {
        Chapter n = c;
        n.start_ms = c.start_ms + delta_ms;
        n.end_ms = c.end_ms == 0 ? 0 : c.end_ms + delta_ms;
        if (n.start_ms < 0) {
            if (c.end_ms != 0 && n.end_ms <= 0) {
                ++dropped;  // the whole chapter lies before the start
                continue;
            }
            n.start_ms = 0;
        }
        out.push_back(n);
    }
    return {out, dropped};
}

}  // namespace samn
