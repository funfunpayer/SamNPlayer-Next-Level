// .samn script container (port of samn/formats/samn_file.py / Go package samn).
#include "samn/samn_file.h"
#include "samn/fileio.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>

#include <nlohmann/json.hpp>


namespace fs = std::filesystem;

namespace samn {

namespace {

// ---------------------------------------------------------------------------
// JSON value tree: ordered objects, number text kept verbatim (Go
// json.RawMessage fidelity), duplicate keys keep the first position and the
// last value (Python dict).
// ---------------------------------------------------------------------------

struct JV {
    enum class K { Null, Bool, Num, Float, Str, Arr, Obj };
    K k = K::Null;
    bool b = false;
    bool is_int = false;  // Num: written without fraction/exponent
    double f = 0;         // Float: rendered like Go
    std::string s;        // Num: source text; Str: value
    std::vector<JV> a;
    std::vector<std::string> keys;
    std::vector<JV> vals;
};

JV JNull() { return JV(); }
JV JBool(bool b) {
    JV v;
    v.k = JV::K::Bool;
    v.b = b;
    return v;
}
JV JInt(int64_t i) {
    JV v;
    v.k = JV::K::Num;
    v.is_int = true;
    v.s = std::to_string(i);
    return v;
}
JV JFloat(double d) {
    JV v;
    v.k = JV::K::Float;
    v.f = d;
    return v;
}
JV JStr(const std::string& s) {
    JV v;
    v.k = JV::K::Str;
    v.s = s;
    return v;
}
JV JArr() {
    JV v;
    v.k = JV::K::Arr;
    return v;
}
JV JObj() {
    JV v;
    v.k = JV::K::Obj;
    return v;
}
void Put(JV& o, const char* key, JV v) {
    o.keys.emplace_back(key);
    o.vals.push_back(std::move(v));
}

const char* TypeName(const JV& v) {
    switch (v.k) {
        case JV::K::Null: return "NoneType";
        case JV::K::Bool: return "bool";
        case JV::K::Num: return v.is_int ? "int" : "float";
        case JV::K::Float: return "float";
        case JV::K::Str: return "str";
        case JV::K::Arr: return "list";
        case JV::K::Obj: return "dict";
    }
    return "?";
}

// Python-style bytes.decode("utf-8", errors="replace"): one U+FFFD per maximal
// invalid subpart.
size_t Utf8Seq(const unsigned char* p, size_t i, size_t n, size_t& bad) {
    const unsigned c = p[i];
    if (c < 0x80) return 1;
    size_t need = 0;
    unsigned lo2 = 0x80, hi2 = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        need = 1;
    } else if (c >= 0xE0 && c <= 0xEF) {
        need = 2;
        if (c == 0xE0) lo2 = 0xA0;
        if (c == 0xED) hi2 = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 3;
        if (c == 0xF0) lo2 = 0x90;
        if (c == 0xF4) hi2 = 0x8F;
    } else {
        bad = 1;
        return 0;
    }
    for (size_t k = 1; k <= need; ++k) {
        const int b = (i + k < n) ? p[i + k] : -1;
        const int lo = k == 1 ? static_cast<int>(lo2) : 0x80;
        const int hi = k == 1 ? static_cast<int>(hi2) : 0xBF;
        if (b < lo || b > hi) {
            bad = k;
            return 0;
        }
    }
    return need + 1;
}

std::string ReplaceInvalidUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const auto* p = reinterpret_cast<const unsigned char*>(in.data());
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        size_t bad = 0;
        const size_t len = Utf8Seq(p, i, n, bad);
        if (len) {
            out.append(in, i, len);
            i += len;
        } else {
            out += "\xEF\xBF\xBD";
            i += bad;
        }
    }
    return out;
}

constexpr int kMaxDepth = 1000;  // Python's recursion limit analogue

class Builder {
public:
    JV root;
    std::string err;

    bool null() { return Add(JNull()); }
    bool boolean(bool v) { return Add(JBool(v)); }
    bool number_integer(nlohmann::json::number_integer_t v) { return Add(JInt(v)); }
    bool number_unsigned(nlohmann::json::number_unsigned_t v) {
        JV n;
        n.k = JV::K::Num;
        n.is_int = true;
        n.s = std::to_string(v);
        return Add(std::move(n));
    }
    bool number_float(nlohmann::json::number_float_t, const std::string& text) {
        JV n;
        n.k = JV::K::Num;
        n.s = text;
        n.is_int = text.find_first_of(".eE") == std::string::npos;
        if (n.is_int && n.s == "-0") n.s = "0";  // Python int("-0") == 0
        return Add(std::move(n));
    }
    bool string(std::string& v) { return Add(JStr(v)); }
    bool binary(nlohmann::json::binary_t&) { return false; }
    bool start_object(std::size_t) { return Open(JObj()); }
    bool key(std::string& k) {
        key_ = k;
        return true;
    }
    bool end_object() {
        st_.pop_back();
        return true;
    }
    bool start_array(std::size_t) { return Open(JArr()); }
    bool end_array() {
        st_.pop_back();
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception& e) {
        if (err.empty()) err = e.what();
        return false;
    }

private:
    std::vector<JV*> st_;
    std::string key_;

    JV* Insert(JV&& v) {
        if (st_.empty()) {
            root = std::move(v);
            return &root;
        }
        JV* top = st_.back();
        if (top->k == JV::K::Arr) {
            top->a.push_back(std::move(v));
            return &top->a.back();
        }
        for (size_t i = 0; i < top->keys.size(); ++i) {
            if (top->keys[i] == key_) {
                top->vals[i] = std::move(v);
                return &top->vals[i];
            }
        }
        top->keys.push_back(key_);
        top->vals.push_back(std::move(v));
        return &top->vals.back();
    }
    bool Add(JV&& v) {
        Insert(std::move(v));
        return true;
    }
    bool Open(JV&& v) {
        if (static_cast<int>(st_.size()) >= kMaxDepth) {
            err = "maximum recursion depth exceeded";
            return false;
        }
        st_.push_back(Insert(std::move(v)));
        return true;
    }
};

// Parses like Python json.loads(bytes.decode("utf-8", "replace")) (strict:
// no NaN/Infinity, no BOM, no trailing data). Returns false and sets err.
bool ParseJson(const std::string& data, JV& out, std::string& err) {
    const std::string text = ReplaceInvalidUtf8(data);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        err = "Unexpected UTF-8 BOM (decode using utf-8-sig)";
        return false;
    }
    Builder b;
    const bool ok = nlohmann::json::sax_parse(text, &b, nlohmann::json::input_format_t::json, true);
    if (!ok) {
        err = b.err.empty() ? "invalid JSON" : b.err;
        return false;
    }
    out = std::move(b.root);
    return true;
}

// ---------------------------------------------------------------------------
// Go-compatible number / string rendering
// ---------------------------------------------------------------------------

std::string GoFloat(double f) {
    if (std::isnan(f)) throw SamnError("json: unsupported value: nan");
    if (std::isinf(f)) throw SamnError(std::string("json: unsupported value: ") + (f < 0 ? "-inf" : "inf"));
    if (f == 0) return std::signbit(f) ? "-0" : "0";
    const double a = std::fabs(f);
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, a, std::chars_format::scientific);
    const std::string sci(buf, r.ptr);  // shortest round-trip, e.g. "1.5e+00"
    const size_t epos = sci.find('e');
    std::string mant = sci.substr(0, epos);
    const int e10 = std::atoi(sci.c_str() + epos + 1);
    std::string ds;
    for (char c : mant)
        if (c != '.') ds += c;
    while (ds.size() > 1 && ds.back() == '0') ds.pop_back();
    std::string out;
    if (a < 1e-6 || a >= 1e21) {
        out = ds.substr(0, 1);
        if (ds.size() > 1) out += "." + ds.substr(1);
        char eb[16];
        if (e10 >= 0)
            std::snprintf(eb, sizeof eb, "e+%02d", e10);
        else
            std::snprintf(eb, sizeof eb, "e-%d", -e10);
        out += eb;
    } else if (e10 >= static_cast<int>(ds.size()) - 1) {
        out = ds + std::string(static_cast<size_t>(e10) - (ds.size() - 1), '0');
    } else if (e10 >= 0) {
        out = ds.substr(0, static_cast<size_t>(e10) + 1) + "." + ds.substr(static_cast<size_t>(e10) + 1);
    } else {
        out = "0." + std::string(static_cast<size_t>(-e10) - 1, '0') + ds;
    }
    return f < 0 ? "-" + out : out;
}

// Go %q for the plain cases used in messages (Python: json.dumps, no ASCII escaping).
std::string GoQuote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
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
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

// Go encoding/json string (HTML escaping on; invalid UTF-8 -> �).
std::string GoString(const std::string& s) {
    std::string out = "\"";
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '<': out += "\\u003c"; break;
                case '>': out += "\\u003e"; break;
                case '&': out += "\\u0026"; break;
                default:
                    if (c < 0x20) {
                        char b[8];
                        std::snprintf(b, sizeof b, "\\u%04x", c);
                        out += b;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
            ++i;
            continue;
        }
        size_t bad = 0;
        const size_t len = Utf8Seq(p, i, n, bad);
        if (!len) {
            out += "\\ufffd";
            ++i;
        } else if (len == 3 && c == 0xE2 && p[i + 1] == 0x80 && (p[i + 2] == 0xA8 || p[i + 2] == 0xA9)) {
            out += p[i + 2] == 0xA8 ? "\\u2028" : "\\u2029";
            i += 3;
        } else {
            out.append(s, i, len);
            i += len;
        }
    }
    return out + "\"";
}

// json.MarshalIndent(v, "", "  ") for the trees built by the encoders.
void Dump(const JV& v, int level, std::string& out) {
    switch (v.k) {
        case JV::K::Null: out += "null"; return;
        case JV::K::Bool: out += v.b ? "true" : "false"; return;
        case JV::K::Num: out += v.s; return;
        case JV::K::Float: out += GoFloat(v.f); return;
        case JV::K::Str: out += GoString(v.s); return;
        case JV::K::Arr: {
            if (v.a.empty()) {
                out += "[]";
                return;
            }
            const std::string pad(static_cast<size_t>(level + 1) * 2, ' ');
            out += "[\n";
            for (size_t i = 0; i < v.a.size(); ++i) {
                if (i) out += ",\n";
                out += pad;
                Dump(v.a[i], level + 1, out);
            }
            out += "\n" + std::string(static_cast<size_t>(level) * 2, ' ') + "]";
            return;
        }
        case JV::K::Obj: {
            if (v.keys.empty()) {
                out += "{}";
                return;
            }
            const std::string pad(static_cast<size_t>(level + 1) * 2, ' ');
            out += "{\n";
            for (size_t i = 0; i < v.keys.size(); ++i) {
                if (i) out += ",\n";
                out += pad + GoString(v.keys[i]) + ": ";
                Dump(v.vals[i], level + 1, out);
            }
            out += "\n" + std::string(static_cast<size_t>(level) * 2, ' ') + "}";
            return;
        }
    }
}

std::string DumpToString(const JV& v) {
    std::string out;
    Dump(v, 0, out);
    return out;
}

// Go marshals maps with keys sorted (byte order).
void SortObject(JV& o) {
    std::vector<size_t> idx(o.keys.size());
    std::iota(idx.begin(), idx.end(), size_t{0});
    std::stable_sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return o.keys[x] < o.keys[y]; });
    std::vector<std::string> keys;
    std::vector<JV> vals;
    for (size_t i : idx) {
        keys.push_back(std::move(o.keys[i]));
        vals.push_back(std::move(o.vals[i]));
    }
    o.keys = std::move(keys);
    o.vals = std::move(vals);
}

// ---------------------------------------------------------------------------
// Encoding (struct order, omitempty)
// ---------------------------------------------------------------------------

JV Enc(const Action& a) {
    JV o = JObj();
    Put(o, "at", JInt(a.at));
    Put(o, "pos", JInt(a.pos));
    return o;
}
JV EncActions(const std::vector<Action>& v) {
    JV a = JArr();
    for (const auto& x : v) a.a.push_back(Enc(x));
    return a;
}
JV EncStrings(const std::vector<std::string>& v) {
    JV a = JArr();
    for (const auto& x : v) a.a.push_back(JStr(x));
    return a;
}
JV EncInts(const std::vector<int64_t>& v) {
    JV a = JArr();
    for (auto x : v) a.a.push_back(JInt(x));
    return a;
}
JV EncDoubles(const std::vector<double>& v) {
    JV a = JArr();
    for (auto x : v) a.a.push_back(JFloat(x));
    return a;
}
JV Enc(const StrengthPreset& p) {
    JV o = JObj();
    Put(o, "name", JStr(p.name));
    Put(o, "vibrationScale", JFloat(p.vibration_scale));
    Put(o, "suctionScale", JFloat(p.suction_scale));
    if (p.contact_span != 0) Put(o, "contactSpan", JFloat(p.contact_span));
    if (!p.contact_curve.empty()) Put(o, "contactCurve", JStr(p.contact_curve));
    return o;
}
JV Enc(const SceneFeelRange& r) {
    JV o = JObj();
    Put(o, "start_ms", JInt(r.start_ms));
    Put(o, "end_ms", JInt(r.end_ms));
    Put(o, "feel", JStr(r.feel));
    return o;
}
JV Enc(const TrackingGap& g) {
    JV o = JObj();
    Put(o, "start_ms", JInt(g.start_ms));
    Put(o, "end_ms", JInt(g.end_ms));
    return o;
}
JV Enc(const DeviceRecipe& r) {
    JV o = JObj();
    Put(o, "sync", JStr(r.sync));
    Put(o, "min_suction", JFloat(r.min_suction));
    Put(o, "tick_ms", JInt(r.tick_ms));
    Put(o, "max_speed", JFloat(r.max_speed));
    Put(o, "smoothing", JFloat(r.smoothing));
    if (r.contact_vibration) Put(o, "contact_vibration", JBool(true));
    if (r.contact_vibration_span != 0) Put(o, "contact_vibration_span", JFloat(r.contact_vibration_span));
    if (!r.contact_vibration_curve.empty()) Put(o, "contact_vibration_curve", JStr(r.contact_vibration_curve));
    if (!r.scene_feel.empty()) Put(o, "scene_feel", JStr(r.scene_feel));
    if (r.vibration_follows_suction) Put(o, "vibration_follows_suction", JBool(true));
    if (r.feel_balance != 0) Put(o, "feel_balance", JFloat(r.feel_balance));
    if (!r.scene_ranges.empty()) {
        JV a = JArr();
        for (const auto& x : r.scene_ranges) a.a.push_back(Enc(x));
        Put(o, "scene_ranges", std::move(a));
    }
    if (!r.playback_source.empty()) Put(o, "playback_source", JStr(r.playback_source));
    return o;
}
JV Enc(const Chapter& c) {
    JV o = JObj();
    Put(o, "name", JStr(c.name));
    Put(o, "startTime", JInt(c.start_ms));
    if (c.end_ms != 0) Put(o, "endTime", JInt(c.end_ms));
    return o;
}
JV Enc(const Bookmark& b) {
    JV o = JObj();
    Put(o, "name", JStr(b.name));
    Put(o, "time", JInt(b.time_ms));
    return o;
}
JV Enc(const OMarker& m) {
    JV o = JObj();
    Put(o, "startMs", JInt(m.start_ms));
    Put(o, "endMs", JInt(m.end_ms));
    Put(o, "kind", JStr(m.kind));
    Put(o, "intensity", JFloat(m.intensity));
    return o;
}
JV Enc(const ContactMarkBox& b) {
    JV o = JObj();
    Put(o, "x", JInt(b.x));
    Put(o, "y", JInt(b.y));
    Put(o, "w", JInt(b.w));
    Put(o, "h", JInt(b.h));
    if (!b.cls.empty()) Put(o, "class", JStr(b.cls));
    if (b.fixed) Put(o, "fixed", JBool(true));
    return o;
}
JV Enc(const ContactMarks& c) {
    JV o = JObj();
    if (!c.tip_class.empty()) Put(o, "tip_class", JStr(c.tip_class));
    if (c.tip) Put(o, "tip", Enc(*c.tip));
    if (c.primary) Put(o, "primary", Enc(*c.primary));
    if (!c.extras.empty()) {
        JV a = JArr();
        for (const auto& x : c.extras) a.a.push_back(Enc(x));
        Put(o, "extras", std::move(a));
    }
    Put(o, "drive_stroke", JBool(c.drive_stroke));
    return o;
}
JV Enc(const TrajectoryPoint& p) {
    JV o = JObj();
    Put(o, "atMs", JInt(p.at_ms));
    Put(o, "x", JFloat(p.x));
    Put(o, "y", JFloat(p.y));
    return o;
}
JV EncPoints(const std::vector<TrajectoryPoint>& v) {
    JV a = JArr();
    for (const auto& x : v) a.a.push_back(Enc(x));
    return a;
}
JV Enc(const TrajectoryData& t) {
    JV o = JObj();
    Put(o, "width", JInt(t.width));
    Put(o, "height", JInt(t.height));
    if (!t.tip.empty()) Put(o, "tip", EncPoints(t.tip));
    if (!t.partner.empty()) Put(o, "partner", EncPoints(t.partner));
    return o;
}
JV Enc(const SceneMapVideo& v) {
    JV o = JObj();
    Put(o, "durationMs", JInt(v.duration_ms));
    Put(o, "width", JInt(v.width));
    Put(o, "height", JInt(v.height));
    if (!v.sha256_head.empty()) Put(o, "sha256_head", JStr(v.sha256_head));
    return o;
}
JV Enc(const SceneMapGrid& g) {
    JV o = JObj();
    Put(o, "cols", JInt(g.cols));
    Put(o, "rows", JInt(g.rows));
    return o;
}
JV Enc(const SceneMapWindow& w) {
    JV o = JObj();
    Put(o, "startMs", JInt(w.start_ms));
    Put(o, "endMs", JInt(w.end_ms));
    Put(o, "tempoHz", JFloat(w.tempo_hz));
    Put(o, "score_b64", JStr(w.score_b64));
    Put(o, "chosen", JInt(w.chosen));
    if (!w.sign_rule.empty()) Put(o, "signRule", JStr(w.sign_rule));
    if (w.tracker_r != 0) Put(o, "trackerR", JFloat(w.tracker_r));
    if (!w.box.empty()) Put(o, "box", EncDoubles(w.box));
    if (!w.marks.empty()) Put(o, "marks", EncStrings(w.marks));
    return o;
}
JV Enc(const SceneMapMarkSample& s) {
    JV o = JObj();
    Put(o, "ms", JInt(s.ms));
    Put(o, "rect", EncInts(s.rect));
    return o;
}
JV Enc(const SceneMapMark& m) {
    JV o = JObj();
    Put(o, "id", JStr(m.id));
    Put(o, "kind", JStr(m.kind));
    Put(o, "rect", EncInts(m.rect));
    if (m.from_ms != 0) Put(o, "fromMs", JInt(m.from_ms));
    if (m.to_ms != 0) Put(o, "toMs", JInt(m.to_ms));
    if (m.at_ms) Put(o, "atMs", JInt(*m.at_ms));
    if (!m.author.empty()) Put(o, "author", JStr(m.author));
    if (!m.created_at.empty()) Put(o, "createdAt", JStr(m.created_at));
    if (!m.cls.empty()) Put(o, "class", JStr(m.cls));
    if (!m.role.empty()) Put(o, "role", JStr(m.role));
    if (m.confidence != 0) Put(o, "confidence", JFloat(m.confidence));
    if (m.reviewed) Put(o, "reviewed", JBool(*m.reviewed));
    if (m.follow) Put(o, "follow", JBool(true));
    if (!m.path.empty()) {
        JV a = JArr();
        for (const auto& x : m.path) a.a.push_back(Enc(x));
        Put(o, "path", std::move(a));
    }
    return o;
}
JV Enc(const SceneMapEvent& e) {
    JV o = JObj();
    Put(o, "atMs", JInt(e.at_ms));
    Put(o, "kind", JStr(e.kind));
    if (!e.from_box.empty()) Put(o, "fromBox", EncInts(e.from_box));
    if (!e.to_box.empty()) Put(o, "toBox", EncInts(e.to_box));
    return o;
}
JV Enc(const SceneMapData& d) {
    JV o = JObj();
    Put(o, "version", JInt(d.version));
    Put(o, "video", Enc(d.video));
    Put(o, "grid", Enc(d.grid));
    if (!d.windows.empty()) {
        JV a = JArr();
        for (const auto& x : d.windows) a.a.push_back(Enc(x));
        Put(o, "windows", std::move(a));
    }
    if (!d.marks.empty()) {
        JV a = JArr();
        for (const auto& x : d.marks) a.a.push_back(Enc(x));
        Put(o, "marks", std::move(a));
    }
    if (!d.events.empty()) {
        JV a = JArr();
        for (const auto& x : d.events) a.a.push_back(Enc(x));
        Put(o, "events", std::move(a));
    }
    return o;
}

template <class T>
JV EncList(const std::vector<T>& v) {
    JV a = JArr();
    for (const auto& x : v) a.a.push_back(Enc(x));
    return a;
}

JV Enc(const Document& d) {
    JV o = JObj();
    Put(o, "version", JInt(d.version));
    Put(o, "kind", JStr(d.kind));
    if (!d.creator.empty()) Put(o, "creator", JStr(d.creator));
    if (d.duration_ms != 0) Put(o, "durationMs", JInt(d.duration_ms));
    if (!d.video_path.empty()) Put(o, "videoPath", JStr(d.video_path));
    if (!d.profile.empty()) Put(o, "profile", JStr(d.profile));
    if (!d.playback_source.empty()) Put(o, "playbackSource", JStr(d.playback_source));
    Put(o, "recipe", Enc(d.recipe));
    Put(o, "general", EncActions(d.general));
    if (!d.vibration.empty()) Put(o, "vibration", EncActions(d.vibration));
    if (!d.suction.empty()) Put(o, "suction", EncActions(d.suction));
    if (!d.chapters.empty()) Put(o, "chapters", EncList(d.chapters));
    if (!d.bookmarks.empty()) Put(o, "bookmarks", EncList(d.bookmarks));
    if (!d.o_markers.empty()) Put(o, "oMarkers", EncList(d.o_markers));
    if (!d.tracking_gaps.empty()) Put(o, "trackingGaps", EncList(d.tracking_gaps));
    if (!d.strength_presets.empty()) Put(o, "strengthPresets", EncList(d.strength_presets));
    if (!d.active_strength.empty()) Put(o, "activeStrength", JStr(d.active_strength));
    if (d.contact_marks) Put(o, "contactMarks", Enc(*d.contact_marks));
    if (d.trajectory) Put(o, "trajectory", Enc(*d.trajectory));
    if (d.scene_map) Put(o, "sceneMap", Enc(*d.scene_map));
    if (d.quality_score) Put(o, "qualityScore", JFloat(*d.quality_score));
    if (d.quality_passed) Put(o, "qualityPassed", JBool(*d.quality_passed));
    if (!d.quality_warnings.empty()) Put(o, "qualityWarnings", EncStrings(d.quality_warnings));
    return o;
}

// ---------------------------------------------------------------------------
// Decoding (Go json.Unmarshal semantics as modelled by the Python port):
// null -> zero value, keys match exactly first then case-insensitively,
// numbers are strict (int fields reject 1.0), unknown keys are ignored.
// ---------------------------------------------------------------------------

[[noreturn]] void Fail(const std::string& msg) { throw SamnError(msg); }

std::string AsciiLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct ObjView {
    const JV& o;
    const JV* Find(const char* name) const {
        for (size_t i = 0; i < o.keys.size(); ++i)
            if (o.keys[i] == name) return &o.vals[i];
        const std::string want = AsciiLower(name);
        const JV* found = nullptr;
        for (size_t i = 0; i < o.keys.size(); ++i)
            if (AsciiLower(o.keys[i]) == want) found = &o.vals[i];  // last wins
        return found;
    }
    template <class T>
    void Get(const char* name, T& out) const;
};

ObjView Need(const JV& v, const char* what) {
    if (v.k != JV::K::Obj) Fail(std::string("cannot unmarshal ") + TypeName(v) + " into " + what);
    return ObjView{v};
}

void DecodeValue(const JV& v, bool& out);
void DecodeValue(const JV& v, int64_t& out);
void DecodeValue(const JV& v, double& out);
void DecodeValue(const JV& v, std::string& out);
void DecodeValue(const JV& v, Action& out);
void DecodeValue(const JV& v, StrengthPreset& out);
void DecodeValue(const JV& v, SceneFeelRange& out);
void DecodeValue(const JV& v, TrackingGap& out);
void DecodeValue(const JV& v, DeviceRecipe& out);
void DecodeValue(const JV& v, Chapter& out);
void DecodeValue(const JV& v, Bookmark& out);
void DecodeValue(const JV& v, OMarker& out);
void DecodeValue(const JV& v, ContactMarkBox& out);
void DecodeValue(const JV& v, ContactMarks& out);
void DecodeValue(const JV& v, TrajectoryPoint& out);
void DecodeValue(const JV& v, TrajectoryData& out);
void DecodeValue(const JV& v, SceneMapVideo& out);
void DecodeValue(const JV& v, SceneMapGrid& out);
void DecodeValue(const JV& v, SceneMapWindow& out);
void DecodeValue(const JV& v, SceneMapMarkSample& out);
void DecodeValue(const JV& v, SceneMapMark& out);
void DecodeValue(const JV& v, SceneMapEvent& out);
void DecodeValue(const JV& v, SceneMapData& out);
void DecodeValue(const JV& v, Document& out);
void DecodeValue(const JV& v, SamnAxes& out);
void DecodeValue(const JV& v, ScriptMetadata& out);
void DecodeValue(const JV& v, FunscriptView& out);

template <class T>
void Dec(const JV& v, T& out);

template <class T>
void DecodeValue(const JV& v, std::vector<T>& out) {
    if (v.k != JV::K::Arr) Fail(std::string("cannot unmarshal ") + TypeName(v) + " into list");
    out.clear();
    for (const auto& e : v.a) {
        T x{};
        Dec(e, x);
        out.push_back(std::move(x));
    }
}

template <class T>
void DecodeValue(const JV& v, std::optional<T>& out) {
    T x{};
    Dec(v, x);
    out = std::move(x);
}

template <class T>
void Dec(const JV& v, T& out) {
    if (v.k == JV::K::Null) {
        out = T();
        return;
    }
    DecodeValue(v, out);
}

template <class T>
void ObjView::Get(const char* name, T& out) const {
    if (const JV* p = Find(name)) Dec(*p, out);
}

void DecodeValue(const JV& v, bool& out) {
    if (v.k != JV::K::Bool) Fail("cannot unmarshal non-bool into bool");
    out = v.b;
}

void DecodeValue(const JV& v, int64_t& out) {
    if (v.k != JV::K::Num || !v.is_int) Fail("cannot unmarshal " + std::string(TypeName(v)) + " into integer");
    errno = 0;
    char* end = nullptr;
    const long long x = std::strtoll(v.s.c_str(), &end, 10);
    if (errno == ERANGE) Fail("number " + v.s + " overflows integer");
    out = x;
}

void DecodeValue(const JV& v, double& out) {
    if (v.k != JV::K::Num) Fail("cannot unmarshal non-number into float");
    out = std::strtod(v.s.c_str(), nullptr);
}

void DecodeValue(const JV& v, std::string& out) {
    if (v.k != JV::K::Str) Fail("cannot unmarshal non-string into string");
    out = v.s;
}

int64_t GoRound(double f) {
    const double r = std::floor(std::fabs(f) + 0.5);
    return static_cast<int64_t>(f >= 0 ? r : -r);
}

// Go: roundNumber on a json.Number (null/missing -> 0, non-numbers fail).
int64_t RoundNumber(const JV* v, const char* what) {
    if (!v || v->k == JV::K::Null) return 0;
    if (v->k != JV::K::Num) Fail(std::string(what) + ": not a number");
    if (v->is_int) {
        errno = 0;
        const long long x = std::strtoll(v->s.c_str(), nullptr, 10);
        if (errno != ERANGE) return x;
    }
    const double f = std::strtod(v->s.c_str(), nullptr);
    return GoRound(std::max(-1e15, std::min(1e15, f)));
}

// Go: Action.UnmarshalJSON (accepts float at/pos, rounds).
void DecodeValue(const JV& v, Action& out) {
    if (v.k != JV::K::Obj) Fail("cannot unmarshal into Action");
    const ObjView o{v};
    out.at = RoundNumber(o.Find("at"), "at");
    const int64_t pos = RoundNumber(o.Find("pos"), "pos");
    out.pos = static_cast<int>(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, pos)));
}

void DecodeValue(const JV& v, StrengthPreset& r) {
    const ObjView o = Need(v, "StrengthPreset");
    o.Get("name", r.name);
    o.Get("vibrationScale", r.vibration_scale);
    o.Get("suctionScale", r.suction_scale);
    o.Get("contactSpan", r.contact_span);
    o.Get("contactCurve", r.contact_curve);
}
void DecodeValue(const JV& v, SceneFeelRange& r) {
    const ObjView o = Need(v, "SceneFeelRange");
    o.Get("start_ms", r.start_ms);
    o.Get("end_ms", r.end_ms);
    o.Get("feel", r.feel);
}
void DecodeValue(const JV& v, TrackingGap& r) {
    const ObjView o = Need(v, "TrackingGap");
    o.Get("start_ms", r.start_ms);
    o.Get("end_ms", r.end_ms);
}
void DecodeValue(const JV& v, DeviceRecipe& r) {
    const ObjView o = Need(v, "DeviceRecipe");
    o.Get("sync", r.sync);
    o.Get("min_suction", r.min_suction);
    o.Get("tick_ms", r.tick_ms);
    o.Get("max_speed", r.max_speed);
    o.Get("smoothing", r.smoothing);
    o.Get("contact_vibration", r.contact_vibration);
    o.Get("contact_vibration_span", r.contact_vibration_span);
    o.Get("contact_vibration_curve", r.contact_vibration_curve);
    o.Get("scene_feel", r.scene_feel);
    o.Get("vibration_follows_suction", r.vibration_follows_suction);
    o.Get("feel_balance", r.feel_balance);
    o.Get("scene_ranges", r.scene_ranges);
    o.Get("playback_source", r.playback_source);
}
void DecodeValue(const JV& v, Chapter& r) {
    const ObjView o = Need(v, "ChapterMark");
    o.Get("name", r.name);
    o.Get("startTime", r.start_ms);
    o.Get("endTime", r.end_ms);
}
void DecodeValue(const JV& v, Bookmark& r) {
    const ObjView o = Need(v, "Bookmark");
    o.Get("name", r.name);
    o.Get("time", r.time_ms);
}
void DecodeValue(const JV& v, OMarker& r) {
    const ObjView o = Need(v, "OMarker");
    o.Get("startMs", r.start_ms);
    o.Get("endMs", r.end_ms);
    o.Get("kind", r.kind);
    o.Get("intensity", r.intensity);
}
void DecodeValue(const JV& v, ContactMarkBox& r) {
    const ObjView o = Need(v, "ContactMarkBox");
    o.Get("x", r.x);
    o.Get("y", r.y);
    o.Get("w", r.w);
    o.Get("h", r.h);
    o.Get("class", r.cls);
    o.Get("fixed", r.fixed);
}
void DecodeValue(const JV& v, ContactMarks& r) {
    const ObjView o = Need(v, "ContactMarks");
    o.Get("tip_class", r.tip_class);
    o.Get("tip", r.tip);
    o.Get("primary", r.primary);
    o.Get("extras", r.extras);
    o.Get("drive_stroke", r.drive_stroke);
}
void DecodeValue(const JV& v, TrajectoryPoint& r) {
    const ObjView o = Need(v, "TrajectoryPoint");
    o.Get("atMs", r.at_ms);
    o.Get("x", r.x);
    o.Get("y", r.y);
}
void DecodeValue(const JV& v, TrajectoryData& r) {
    const ObjView o = Need(v, "TrajectoryData");
    o.Get("width", r.width);
    o.Get("height", r.height);
    o.Get("tip", r.tip);
    o.Get("partner", r.partner);
}
void DecodeValue(const JV& v, SceneMapVideo& r) {
    const ObjView o = Need(v, "SceneMapVideo");
    o.Get("durationMs", r.duration_ms);
    o.Get("width", r.width);
    o.Get("height", r.height);
    o.Get("sha256_head", r.sha256_head);
}
void DecodeValue(const JV& v, SceneMapGrid& r) {
    const ObjView o = Need(v, "SceneMapGrid");
    o.Get("cols", r.cols);
    o.Get("rows", r.rows);
}
void DecodeValue(const JV& v, SceneMapWindow& r) {
    const ObjView o = Need(v, "SceneMapWindow");
    o.Get("startMs", r.start_ms);
    o.Get("endMs", r.end_ms);
    o.Get("tempoHz", r.tempo_hz);
    o.Get("score_b64", r.score_b64);
    o.Get("chosen", r.chosen);
    o.Get("signRule", r.sign_rule);
    o.Get("trackerR", r.tracker_r);
    o.Get("box", r.box);
    o.Get("marks", r.marks);
}
void DecodeValue(const JV& v, SceneMapMarkSample& r) {
    const ObjView o = Need(v, "SceneMapMarkSample");
    o.Get("ms", r.ms);
    o.Get("rect", r.rect);
}
void DecodeValue(const JV& v, SceneMapMark& r) {
    const ObjView o = Need(v, "SceneMapMark");
    o.Get("id", r.id);
    o.Get("kind", r.kind);
    o.Get("rect", r.rect);
    o.Get("fromMs", r.from_ms);
    o.Get("toMs", r.to_ms);
    o.Get("atMs", r.at_ms);
    o.Get("author", r.author);
    o.Get("createdAt", r.created_at);
    o.Get("class", r.cls);
    o.Get("role", r.role);
    o.Get("confidence", r.confidence);
    o.Get("reviewed", r.reviewed);
    o.Get("follow", r.follow);
    o.Get("path", r.path);
}
void DecodeValue(const JV& v, SceneMapEvent& r) {
    const ObjView o = Need(v, "SceneMapEvent");
    o.Get("atMs", r.at_ms);
    o.Get("kind", r.kind);
    o.Get("fromBox", r.from_box);
    o.Get("toBox", r.to_box);
}
void DecodeValue(const JV& v, SceneMapData& r) {
    const ObjView o = Need(v, "SceneMapData");
    o.Get("version", r.version);
    o.Get("video", r.video);
    o.Get("grid", r.grid);
    o.Get("windows", r.windows);
    o.Get("marks", r.marks);
    o.Get("events", r.events);
}
void DecodeValue(const JV& v, Document& r) {
    const ObjView o = Need(v, "Document");
    o.Get("version", r.version);
    o.Get("kind", r.kind);
    o.Get("creator", r.creator);
    o.Get("durationMs", r.duration_ms);
    o.Get("videoPath", r.video_path);
    o.Get("profile", r.profile);
    o.Get("playbackSource", r.playback_source);
    o.Get("recipe", r.recipe);
    o.Get("general", r.general);
    o.Get("vibration", r.vibration);
    o.Get("suction", r.suction);
    o.Get("chapters", r.chapters);
    o.Get("bookmarks", r.bookmarks);
    o.Get("oMarkers", r.o_markers);
    o.Get("trackingGaps", r.tracking_gaps);
    o.Get("strengthPresets", r.strength_presets);
    o.Get("activeStrength", r.active_strength);
    o.Get("contactMarks", r.contact_marks);
    o.Get("trajectory", r.trajectory);
    o.Get("sceneMap", r.scene_map);
    o.Get("qualityScore", r.quality_score);
    o.Get("qualityPassed", r.quality_passed);
    o.Get("qualityWarnings", r.quality_warnings);
}
void DecodeValue(const JV& v, SamnAxes& r) {
    const ObjView o = Need(v, "SamnAxes");
    o.Get("vibration", r.vibration);
    o.Get("suction", r.suction);
}

nlohmann::json ToScalarJson(const JV& v) {
    switch (v.k) {
        case JV::K::Null: return nullptr;
        case JV::K::Bool: return v.b;
        case JV::K::Num:
            if (v.is_int) {
                errno = 0;
                const long long x = std::strtoll(v.s.c_str(), nullptr, 10);
                if (errno != ERANGE) return static_cast<int64_t>(x);
            }
            return std::strtod(v.s.c_str(), nullptr);
        case JV::K::Float: return v.f;
        case JV::K::Str: return v.s;
        case JV::K::Arr: return nlohmann::json::array();
        case JV::K::Obj: return nlohmann::json::object();
    }
    return nullptr;
}

// Go: flexTimeMs on metadata.duration; unreadable values count as 0.
int64_t MetaDuration(const JV& v) {
    try {
        return FlexTimeMs(ToScalarJson(v));
    } catch (const std::exception&) {
        return 0;
    }
}

void DecodeValue(const JV& v, ScriptMetadata& r) {
    const ObjView o = Need(v, "ScriptMetadata");
    if (const JV* d = o.Find("duration")) r.duration = MetaDuration(*d);
    o.Get("creator", r.creator);
    o.Get("quality_score", r.quality_score);
    o.Get("quality_passed", r.quality_passed);
    o.Get("quality_warnings", r.quality_warnings);
    o.Get("profile", r.profile);
    o.Get("device_recipe", r.device_recipe);
    o.Get("samn_axes", r.samn_axes);
    o.Get("tracking_gaps", r.tracking_gaps);
    o.Get("trajectory", r.trajectory);
    o.Get("contact_marks", r.contact_marks);
}
void DecodeValue(const JV& v, FunscriptView& r) {
    const ObjView o = Need(v, "Script");
    o.Get("actions", r.actions);
    o.Get("inverted", r.inverted);
    o.Get("metadata", r.metadata);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool ReadAll(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return false;
    out = ss.str();
    return true;
}

// Errors as SamnError, as before (fileio.h does the writing).
void WriteAtomic(const fs::path& target, const std::string& data) {
    try {
        WriteFileAtomic(target, data);
    } catch (const std::exception&) {
        throw SamnError("samn: write: " + target.string());
    }
}

// ---------------------------------------------------------------------------
// Equality helpers
// ---------------------------------------------------------------------------

bool Same(const Action& a, const Action& b) { return a.at == b.at && a.pos == b.pos; }
bool Same(const StrengthPreset& a, const StrengthPreset& b) {
    return a.name == b.name && a.vibration_scale == b.vibration_scale && a.suction_scale == b.suction_scale &&
           a.contact_span == b.contact_span && a.contact_curve == b.contact_curve;
}
bool Same(const SceneFeelRange& a, const SceneFeelRange& b) {
    return a.start_ms == b.start_ms && a.end_ms == b.end_ms && a.feel == b.feel;
}
bool Same(const TrackingGap& a, const TrackingGap& b) { return a.start_ms == b.start_ms && a.end_ms == b.end_ms; }
bool Same(const Chapter& a, const Chapter& b) { return a == b; }
bool Same(const Bookmark& a, const Bookmark& b) { return a == b; }
bool Same(const OMarker& a, const OMarker& b) {
    return a.start_ms == b.start_ms && a.end_ms == b.end_ms && a.kind == b.kind && a.intensity == b.intensity;
}
bool Same(const DeviceRecipe& a, const DeviceRecipe& b);
bool Same(const ContactMarkBox& a, const ContactMarkBox& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.cls == b.cls && a.fixed == b.fixed;
}
bool Same(const TrajectoryPoint& a, const TrajectoryPoint& b) {
    return a.at_ms == b.at_ms && a.x == b.x && a.y == b.y;
}
bool Same(const SceneMapMarkSample& a, const SceneMapMarkSample& b) { return a.ms == b.ms && a.rect == b.rect; }
bool Same(const SceneMapEvent& a, const SceneMapEvent& b) {
    return a.at_ms == b.at_ms && a.kind == b.kind && a.from_box == b.from_box && a.to_box == b.to_box;
}
bool Same(const SceneMapWindow& a, const SceneMapWindow& b) {
    return a.start_ms == b.start_ms && a.end_ms == b.end_ms && a.tempo_hz == b.tempo_hz &&
           a.score_b64 == b.score_b64 && a.chosen == b.chosen && a.sign_rule == b.sign_rule &&
           a.tracker_r == b.tracker_r && a.box == b.box && a.marks == b.marks;
}
template <class T>
bool Same(const std::vector<T>& a, const std::vector<T>& b);
template <class T>
bool Same(const std::optional<T>& a, const std::optional<T>& b);
bool Same(const SceneMapMark& a, const SceneMapMark& b) {
    return a.id == b.id && a.kind == b.kind && a.rect == b.rect && a.from_ms == b.from_ms && a.to_ms == b.to_ms &&
           a.at_ms == b.at_ms && a.author == b.author && a.created_at == b.created_at && a.cls == b.cls &&
           a.role == b.role && a.confidence == b.confidence && a.reviewed == b.reviewed && a.follow == b.follow &&
           Same(a.path, b.path);
}
bool Same(const ContactMarks& a, const ContactMarks& b) {
    return a.tip_class == b.tip_class && Same(a.tip, b.tip) && Same(a.primary, b.primary) &&
           Same(a.extras, b.extras) && a.drive_stroke == b.drive_stroke;
}
bool Same(const TrajectoryData& a, const TrajectoryData& b) {
    return a.width == b.width && a.height == b.height && Same(a.tip, b.tip) && Same(a.partner, b.partner);
}
bool Same(const SceneMapData& a, const SceneMapData& b) {
    return a.version == b.version && a.video.duration_ms == b.video.duration_ms && a.video.width == b.video.width &&
           a.video.height == b.video.height && a.video.sha256_head == b.video.sha256_head &&
           a.grid.cols == b.grid.cols && a.grid.rows == b.grid.rows && Same(a.windows, b.windows) &&
           Same(a.marks, b.marks) && Same(a.events, b.events);
}
bool Same(const DeviceRecipe& a, const DeviceRecipe& b) {
    return a.sync == b.sync && a.min_suction == b.min_suction && a.tick_ms == b.tick_ms &&
           a.max_speed == b.max_speed && a.smoothing == b.smoothing && a.contact_vibration == b.contact_vibration &&
           a.contact_vibration_span == b.contact_vibration_span &&
           a.contact_vibration_curve == b.contact_vibration_curve && a.scene_feel == b.scene_feel &&
           a.vibration_follows_suction == b.vibration_follows_suction && a.feel_balance == b.feel_balance &&
           Same(a.scene_ranges, b.scene_ranges) && a.playback_source == b.playback_source;
}
template <class T>
bool Same(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!Same(a[i], b[i])) return false;
    return true;
}
template <class T>
bool Same(const std::optional<T>& a, const std::optional<T>& b) {
    if (a.has_value() != b.has_value()) return false;
    return !a.has_value() || Same(*a, *b);
}

std::string GoExt(const std::string& path) {
    for (size_t i = path.size(); i-- > 0;) {
        if (path[i] == '/') break;
        if (path[i] == '.') return path.substr(i);
    }
    return "";
}

}  // namespace

std::string NormalizeSceneFeel(const std::string& name) {
    std::string n = AsciiLower(name);
    const auto b = n.find_first_not_of(" \t\n\r\f\v");
    n = b == std::string::npos ? "" : n.substr(b, n.find_last_not_of(" \t\n\r\f\v") - b + 1);
    auto in = [&](std::initializer_list<const char*> l) {
        for (const char* s : l)
            if (n == s) return true;
        return false;
    };
    if (in({"oral", "blowjob", "bj", "mouth", "lips"})) return "oral";
    if (in({"intercourse", "penetration", "sex"})) return "intercourse";
    if (in({"breast", "titjob", "tj", "boobjob", "nipple", "nipples"})) return "breast";
    if (in({"hand", "handjob", "hj"})) return "hand";
    return "";
}

namespace {

std::vector<SceneFeelRange> NormalizeSceneRanges(const std::vector<SceneFeelRange>& rs) {
    std::vector<SceneFeelRange> out;
    for (const auto& r : rs)
        if (r.end_ms > r.start_ms) out.push_back({r.start_ms, r.end_ms, NormalizeSceneFeel(r.feel)});
    return out;
}

std::string StripLower(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return "";
    return AsciiLower(s.substr(b, s.find_last_not_of(" \t\n\r\f\v") - b + 1));
}

}  // namespace

// ---------------------------------------------------------------------------
// OMarker / Document / FunscriptView
// ---------------------------------------------------------------------------

void OMarker::Validate() const {
    if (end_ms <= start_ms)
        throw SamnError("funscript: O-marker end (" + std::to_string(end_ms) + "ms) must be after the start (" +
                        std::to_string(start_ms) + "ms)");
    if (kind != OMarkerPrimary && kind != OMarkerSecondary)
        throw SamnError("funscript: unknown O-marker kind " + GoQuote(kind) + " (expected \"" + OMarkerPrimary +
                        "\" or \"" + OMarkerSecondary + "\")");
    if (intensity < 0 || intensity > 1)
        throw SamnError("funscript: O-marker intensity " + GoFloat(intensity) + " outside 0..1");
}

std::vector<Action> FunscriptView::PlaybackActions() const { return samn::PlaybackActions(actions, inverted); }

bool operator==(const Document& a, const Document& b) {
    return a.version == b.version && a.kind == b.kind && a.creator == b.creator && a.duration_ms == b.duration_ms &&
           a.video_path == b.video_path && a.profile == b.profile && a.playback_source == b.playback_source &&
           Same(a.recipe, b.recipe) && Same(a.general, b.general) && Same(a.vibration, b.vibration) &&
           Same(a.suction, b.suction) && Same(a.chapters, b.chapters) && Same(a.bookmarks, b.bookmarks) &&
           Same(a.o_markers, b.o_markers) && Same(a.tracking_gaps, b.tracking_gaps) &&
           Same(a.strength_presets, b.strength_presets) && a.active_strength == b.active_strength &&
           Same(a.contact_marks, b.contact_marks) && Same(a.trajectory, b.trajectory) &&
           Same(a.scene_map, b.scene_map) && a.quality_score == b.quality_score &&
           a.quality_passed == b.quality_passed && a.quality_warnings == b.quality_warnings;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

bool IsSamnPath(const std::string& path) { return AsciiLower(GoExt(path)) == SamnExt; }

std::string CompanionSamnPath(const std::string& path) {
    return path.substr(0, path.size() - GoExt(path).size()) + SamnExt;
}

std::string CompanionFunscriptPath(const std::string& path) {
    return path.substr(0, path.size() - GoExt(path).size()) + ".funscript";
}

std::string PreferSamnCompanion(const std::string& path) {
    if (path.empty() || IsSamnPath(path)) return path;
    const std::string companion = CompanionSamnPath(path);
    std::error_code ec;
    if (fs::is_regular_file(fs::path(companion), ec)) return companion;
    return path;
}

// ---------------------------------------------------------------------------
// Normalize / validation
// ---------------------------------------------------------------------------

std::string NormalizePlaybackSource(const std::string& s) {
    return StripLower(s) == PlaybackAxes ? PlaybackAxes : PlaybackRecipe;
}

std::string NormalizeProfile(const std::string& name) {
    const std::string n = StripLower(name);
    if (n == "tf" || n == "tj") return ProfileTj;
    if (n == "weich") return ProfileWeich;
    if (n.empty() || n == "standard") return ProfileStandard;
    return n;
}

bool IsDistanceProfile(const std::string& name) {
    const std::string n = NormalizeProfile(name);
    return n == ProfileTj || n == ProfileTf;
}

std::vector<Action> SanitizeActions(const std::vector<Action>& actions) {
    if (actions.empty()) throw SamnError("funscript: at least one point is needed");
    std::vector<Action> out;
    out.reserve(actions.size());
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].at < 0)
            throw SamnError("funscript: point " + std::to_string(i) + " has a negative time (" +
                            std::to_string(actions[i].at) + "ms)");
        out.push_back({actions[i].at, std::max(0, std::min(100, actions[i].pos))});
    }
    std::stable_sort(out.begin(), out.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
    return out;
}

void Document::Normalize() {
    if (version == 0) version = SamnCurrentVersion;
    if (version != SamnCurrentVersion)
        throw SamnError("samn: unsupported version " + std::to_string(version) + " (want " +
                        std::to_string(SamnCurrentVersion) + ")");
    if (kind.empty()) kind = SamnKind;
    if (kind != SamnKind) throw SamnError("samn: unexpected kind " + GoQuote(kind));
    if (general.empty()) throw SamnError("samn: general curve is required");
    general = SanitizeActions(general);
    if (!vibration.empty()) vibration = SanitizeActions(vibration);
    if (!suction.empty()) suction = SanitizeActions(suction);
    playback_source = NormalizePlaybackSource(playback_source);
    profile = NormalizeProfile(profile);
    if (duration_ms <= 0) duration_ms = general.back().at;
    for (size_t i = 0; i < o_markers.size(); ++i) {
        try {
            o_markers[i].Validate();
        } catch (const SamnError& e) {
            throw SamnError("samn: oMarkers[" + std::to_string(i) + "]: " + e.what());
        }
    }
}

// ---------------------------------------------------------------------------
// I/O: Parse / Load / Save
// ---------------------------------------------------------------------------

Document ParseDocument(const std::string& data) {
    JV root;
    std::string err;
    if (!ParseJson(data, root, err)) throw SamnError("samn: invalid JSON: " + err);
    Document d;
    if (root.k == JV::K::Null) {
        // JSON null: Go leaves the zero Document
    } else if (root.k == JV::K::Obj) {
        try {
            DecodeValue(root, d);
        } catch (const SamnError& e) {
            throw SamnError(std::string("samn: invalid JSON: ") + e.what());
        }
    } else {
        throw SamnError(std::string("samn: invalid JSON: cannot unmarshal ") + TypeName(root) +
                        " into Go value of type samn.Document");
    }
    d.Normalize();
    return d;
}

Document LoadDocument(const fs::path& path) {
    std::string data;
    if (!ReadAll(path, data)) throw SamnError("samn: read: cannot read " + path.string());
    return ParseDocument(data);
}

std::string MarshalDocument(const Document& d) { return DumpToString(Enc(d)); }

void SaveDocument(const fs::path& path, Document& d) {
    d.Normalize();
    WriteAtomic(path, MarshalDocument(d));
}

// ---------------------------------------------------------------------------
// funscript side
// ---------------------------------------------------------------------------

FunscriptView ParseFunscript(const std::string& data) {
    FunscriptView s;
    {
        JV root;
        std::string err;
        if (!ParseJson(data, root, err)) throw SamnError("funscript: invalid JSON: " + err);
        try {
            if (root.k != JV::K::Obj) Fail("cannot unmarshal into Script");
            DecodeValue(root, s);
        } catch (const SamnError& e) {
            throw SamnError(std::string("funscript: invalid JSON: ") + e.what());
        }
    }
    if (s.actions.empty()) throw SamnError("funscript: no actions found in the script");
    std::vector<Action> kept;
    for (const auto& a : s.actions)
        if (a.at >= 0 && a.at <= MaxActionMs) kept.push_back(a);
    s.dropped = static_cast<int>(s.actions.size() - kept.size());
    if (s.dropped > 0) {
        s.actions = kept;
        if (s.actions.empty())
            throw SamnError("funscript: no valid actions (all times outside 0–" +
                            std::to_string(MaxActionMs) + " ms)");
    }
    for (auto& a : s.actions) a.pos = std::max(0, std::min(100, a.pos));
    std::stable_sort(s.actions.begin(), s.actions.end(),
                     [](const Action& a, const Action& b) { return a.at < b.at; });
    const int64_t d = s.metadata.duration, last = s.Duration();
    if (d > 0 && d * 100 < last) s.metadata.duration = d * 1000;
    return s;
}

FunscriptView LoadFunscript(const fs::path& path) {
    std::string data;
    if (!ReadAll(path, data))
        throw SamnError("funscript: cannot read the file: " + path.string());
    return ParseFunscript(data);
}

ScriptMarks LoadScriptMarks(const fs::path& path) {
    ScriptMarks m;
    if (IsSamnPath(path.string())) {
        Document d = LoadDocument(path);
        m.bookmarks = std::move(d.bookmarks);
        m.chapters = std::move(d.chapters);
        m.o_markers = std::move(d.o_markers);
        return m;
    }
    m.bookmarks = LoadBookmarks(path);
    m.chapters = LoadChapters(path);
    m.o_markers = LoadOMarkers(path);
    return m;
}

void SaveScriptBookmarks(const fs::path& path, const std::vector<Bookmark>& bookmarks) {
    if (!IsSamnPath(path.string())) return SaveBookmarks(path, bookmarks);
    Document d = LoadDocument(path);
    d.bookmarks = bookmarks;
    SaveDocument(path, d);
}

void SaveScriptChapters(const fs::path& path, const std::vector<Chapter>& chapters) {
    if (!IsSamnPath(path.string())) return SaveChapters(path, chapters);
    Document d = LoadDocument(path);
    d.chapters = chapters;
    SaveDocument(path, d);
}

void SaveScriptOMarkers(const fs::path& path, const std::vector<OMarker>& markers) {
    if (!IsSamnPath(path.string())) return SaveOMarkers(path, markers);
    for (size_t i = 0; i < markers.size(); ++i) {
        try {
            markers[i].Validate();
        } catch (const SamnError& e) {
            throw SamnError("oMarkers[" + std::to_string(i) + "]: " + e.what());
        }
    }
    Document d = LoadDocument(path);
    d.o_markers = markers;
    SaveDocument(path, d);
}

void SaveOMarkers(const fs::path& path, const std::vector<OMarker>& markers) {
    for (size_t i = 0; i < markers.size(); ++i) {
        try {
            markers[i].Validate();
        } catch (const SamnError& e) {
            throw SamnError("funscript: O-marker " + std::to_string(i) + ": " + e.what());
        }
    }
    std::vector<OMarker> sorted = markers;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const OMarker& a, const OMarker& b) { return a.start_ms < b.start_ms; });
    std::string encoded;
    if (sorted.empty()) {
        encoded = "null";
    } else {
        encoded = "[";
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i) encoded += ',';
            encoded += "{\"startMs\":" + std::to_string(sorted[i].start_ms) + ",\"endMs\":" +
                       std::to_string(sorted[i].end_ms) + ",\"kind\":" + GoString(sorted[i].kind) +
                       ",\"intensity\":" + GoFloat(sorted[i].intensity) + "}";
        }
        encoded += "]";
    }
    PatchMetadataKey(path, "oMarkers", encoded, /*indented=*/true);
}

std::vector<OMarker> LoadOMarkers(const fs::path& path) {
    std::string data;
    if (!ReadAll(path, data))
        throw SamnError("funscript: cannot read the file: " + path.string());
    JV root;
    std::string err;
    if (!ParseJson(data, root, err)) throw SamnError("funscript: invalid JSON: " + err);
    try {
        if (root.k != JV::K::Obj) Fail("not an object");
        // Python: doc.get("metadata") or {}  (exact key, falsy -> empty)
        const JV* exact = nullptr;
        for (size_t i = 0; i < root.keys.size(); ++i)
            if (root.keys[i] == "metadata") exact = &root.vals[i];
        if (!exact) return {};
        const JV& m = *exact;
        const bool falsy = m.k == JV::K::Null || (m.k == JV::K::Bool && !m.b) ||
                           (m.k == JV::K::Num && std::strtod(m.s.c_str(), nullptr) == 0) ||
                           (m.k == JV::K::Str && m.s.empty()) || (m.k == JV::K::Arr && m.a.empty()) ||
                           (m.k == JV::K::Obj && m.keys.empty());
        if (falsy) return {};
        if (m.k != JV::K::Obj) Fail("invalid metadata");
        const JV* raw = nullptr;
        for (size_t i = 0; i < m.keys.size(); ++i)
            if (m.keys[i] == "oMarkers") raw = &m.vals[i];
        if (!raw || raw->k == JV::K::Null) return {};
        std::vector<OMarker> out;
        DecodeValue(*raw, out);
        return out;
    } catch (const SamnError& e) {
        throw SamnError(std::string("funscript: invalid JSON: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Recipes
// ---------------------------------------------------------------------------

MapOptions RecipeFor(const std::string& name) {
    MapOptions opts = DefaultMapOptions();
    const std::string n = NormalizeProfile(name);
    if (n == ProfileTj || n == ProfileTf) {
        opts.sync = SyncMode::SuctionPosition;
        opts.min_vibration = 0;
        opts.min_suction = 0.20;
        opts.tick_ms = 50;
        opts.max_speed = 0.50;
        opts.smoothing = 0.22;
    } else if (n == ProfileWeich) {
        opts.sync = SyncMode::Independent;
        opts.min_vibration = 0.12;
        opts.min_suction = 0.10;
        opts.smoothing = 0.28;
    }
    return opts;
}

DeviceRecipe RecipeMeta(const std::string& name) {
    const MapOptions o = RecipeFor(name);
    DeviceRecipe r;
    r.sync = SyncModeName(o.sync);
    r.min_suction = o.min_suction;
    r.tick_ms = o.tick_ms;
    r.max_speed = o.max_speed;
    r.smoothing = o.smoothing;
    return r;
}

std::string DefaultSyncForProfile(const std::string& profile) {
    return SyncModeName(IsDistanceProfile(profile) ? SyncMode::SuctionPosition : SyncMode::Independent);
}

MapOptions MapOptionsFromScript(const FunscriptView& s) {
    MapOptions opts = DefaultMapOptions();
    if (!s.metadata.profile.empty()) opts = RecipeFor(s.metadata.profile);
    if (s.metadata.device_recipe) {
        const DeviceRecipe& dr = *s.metadata.device_recipe;
        if (!dr.sync.empty()) {
            try {
                opts.sync = ParseSyncMode(dr.sync);
            } catch (const std::invalid_argument&) {
            }
        }
        if (dr.tick_ms > 0) opts.tick_ms = dr.tick_ms;
        if (dr.max_speed > 0) opts.max_speed = dr.max_speed;
        if (dr.smoothing > 0) opts.smoothing = dr.smoothing;
        if (dr.min_suction > 0) opts.min_suction = dr.min_suction;
        opts.contact_vibration = dr.contact_vibration;
        opts.contact_vibration_span = dr.contact_vibration_span;
        opts.contact_vibration_curve = dr.contact_vibration_curve;
        opts.scene_feel = NormalizeSceneFeel(dr.scene_feel);
        opts.scene_ranges = NormalizeSceneRanges(dr.scene_ranges);
        opts.vibration_follows_suction = dr.vibration_follows_suction;
        opts.balance = dr.feel_balance;
        if (NormalizePlaybackSource(dr.playback_source) == PlaybackAxes && s.metadata.samn_axes) {
            opts.use_explicit_axes = true;
            opts.vibration_axis = s.metadata.samn_axes->vibration;
            opts.suction_axis = s.metadata.samn_axes->suction;
        }
    }
    if (!s.metadata.tracking_gaps.empty()) opts.tracking_gaps = s.metadata.tracking_gaps;
    return opts;
}

// ---------------------------------------------------------------------------
// convert.go
// ---------------------------------------------------------------------------

Document FromFunscript(const FunscriptView& s, const std::string& video_path) {
    const ScriptMetadata& m = s.metadata;
    Document d;
    d.version = SamnCurrentVersion;
    d.kind = SamnKind;
    d.video_path = video_path;
    d.creator = m.creator;
    d.duration_ms = m.duration;
    d.profile = m.profile;
    d.general = s.PlaybackActions();  // an OFS "inverted" flag is baked in
    if (d.duration_ms <= 0) d.duration_ms = s.Duration();
    if (m.device_recipe) {
        d.recipe = *m.device_recipe;
        d.playback_source = NormalizePlaybackSource(m.device_recipe->playback_source);
    } else {
        d.playback_source = PlaybackRecipe;
    }
    if (m.samn_axes) {
        d.vibration = m.samn_axes->vibration;
        d.suction = m.samn_axes->suction;
    }
    d.tracking_gaps = m.tracking_gaps;
    d.quality_score = m.quality_score;
    d.quality_passed = m.quality_passed;
    d.quality_warnings = m.quality_warnings;
    d.contact_marks = m.contact_marks;
    d.trajectory = m.trajectory;
    return d;
}

Document LoadFunscriptFile(const fs::path& path) {
    const FunscriptView s = LoadFunscript(path);
    Document d = FromFunscript(s, "");
    // Chapters/bookmarks live outside the typed metadata - load separately.
    try {
        d.chapters = LoadChapters(path);
    } catch (const std::exception&) {
    }
    try {
        d.bookmarks = LoadBookmarks(path);
    } catch (const std::exception&) {
    }
    try {
        d.o_markers = LoadOMarkers(path);
    } catch (const std::exception&) {
    }
    try {
        d.Normalize();
    } catch (const SamnError&) {
    }
    return d;
}

FunscriptView Document::ToFunscript() {
    Normalize();
    FunscriptView s;
    s.actions = general;
    ScriptMetadata& m = s.metadata;
    m.creator = creator;
    m.duration = duration_ms;
    m.profile = profile;
    m.quality_score = quality_score;
    m.quality_passed = quality_passed;
    m.quality_warnings = quality_warnings;
    m.tracking_gaps = tracking_gaps;
    m.contact_marks = contact_marks;
    m.trajectory = trajectory;

    DeviceRecipe r = recipe;
    r.playback_source = NormalizePlaybackSource(playback_source);
    if (r.sync.empty() && IsDistanceProfile(profile)) {
        const DeviceRecipe meta = RecipeMeta(profile);
        if (r.tick_ms == 0)
            r = meta;
        else
            r.sync = meta.sync;
        r.playback_source = NormalizePlaybackSource(playback_source);
    }
    m.device_recipe = r;

    if (!vibration.empty() || !suction.empty()) {
        SamnAxes ax;
        ax.vibration = vibration;
        ax.suction = suction;
        m.samn_axes = ax;
    }
    return s;
}

void Document::ExportFunscript(const fs::path& path) {
    Normalize();
    // Metadata keys this export writes from the document (or drops).
    static const char* const kManaged[] = {"creator",       "duration",       "profile",
                                           "device_recipe", "tracking_gaps",  "contact_marks",
                                           "trajectory",    "quality_score",  "quality_passed",
                                           "quality_warnings", "chapters",    "bookmarks",
                                           "oMarkers",      "samn_axes"};
    JV meta = JObj();
    Put(meta, "creator", JStr(creator));
    Put(meta, "duration", JInt(duration_ms));
    if (!profile.empty()) Put(meta, "profile", JStr(profile));
    if (!recipe.sync.empty() || recipe.contact_vibration || recipe.tick_ms > 0) {
        DeviceRecipe rec = recipe;
        rec.playback_source.clear();  // foreign tools always get recipe playback
        Put(meta, "device_recipe", Enc(rec));
    }
    if (!tracking_gaps.empty()) Put(meta, "tracking_gaps", EncList(tracking_gaps));
    if (contact_marks) Put(meta, "contact_marks", Enc(*contact_marks));
    if (trajectory && !trajectory->tip.empty()) Put(meta, "trajectory", Enc(*trajectory));
    if (quality_score) Put(meta, "quality_score", JFloat(*quality_score));
    if (quality_passed) Put(meta, "quality_passed", JBool(*quality_passed));
    if (!quality_warnings.empty()) Put(meta, "quality_warnings", EncStrings(quality_warnings));
    if (!chapters.empty()) Put(meta, "chapters", EncList(chapters));
    if (!bookmarks.empty()) Put(meta, "bookmarks", EncList(bookmarks));
    if (!o_markers.empty()) Put(meta, "oMarkers", EncList(o_markers));

    JV doc = JObj();
    std::string old_text;
    JV old;
    std::string err;
    if (ReadAll(path, old_text) && ParseJson(old_text, old, err) && old.k == JV::K::Obj) {
        const JV* old_meta = nullptr;
        for (size_t i = 0; i < old.keys.size(); ++i) {
            const std::string& k = old.keys[i];
            if (k == "metadata") old_meta = &old.vals[i];
            if (k != "actions" && k != "metadata" && k != "inverted") Put(doc, k.c_str(), old.vals[i]);
        }
        if (old_meta && old_meta->k == JV::K::Obj) {
            for (size_t i = 0; i < old_meta->keys.size(); ++i) {
                const std::string& k = old_meta->keys[i];
                bool managed = false;
                for (const char* m : kManaged)
                    if (k == m) managed = true;
                if (managed) continue;
                if (std::find(meta.keys.begin(), meta.keys.end(), k) == meta.keys.end())
                    Put(meta, k.c_str(), old_meta->vals[i]);
            }
        }
    }
    Put(doc, "actions", EncActions(general));
    SortObject(meta);
    Put(doc, "metadata", std::move(meta));
    SortObject(doc);
    WriteAtomic(path, DumpToString(doc));
}

namespace {

std::vector<Action> FramesToActions(const std::vector<Frame>& frames, bool vibration, int64_t max_step_ms) {
    if (frames.empty()) return {{0, 0}};
    auto pos_of = [&](const Frame& f) {
        const double v = vibration ? f.vibration : f.suction;
        return static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(100, GoRound(Clamp01(v) * 100))));
    };
    std::vector<Action> out;
    int64_t last_at = -(int64_t{1} << 62);
    int last_pos = -1;
    for (const auto& f : frames) {
        const int pos = pos_of(f);
        if (last_pos < 0 || pos != last_pos || f.at - last_at >= max_step_ms) {
            out.push_back({f.at, pos});
            last_at = f.at;
            last_pos = pos;
        }
    }
    const Frame& last = frames.back();
    const int pos = pos_of(last);
    if (out.back().at != last.at || out.back().pos != pos) out.push_back({last.at, pos});
    return out;
}

}  // namespace

std::pair<std::vector<Action>, std::vector<Action>> BakeAxesFromRecipe(const std::vector<Action>& actions,
                                                                      MapOptions opts, int64_t max_step_ms) {
    opts.use_explicit_axes = false;
    opts.vibration_axis.clear();
    opts.suction_axis.clear();
    const auto curve = ToIntensityCurve(actions, opts);
    const std::vector<Frame> frames = curve ? *curve : std::vector<Frame>{};
    if (max_step_ms <= 0) max_step_ms = 200;
    return {FramesToActions(frames, true, max_step_ms), FramesToActions(frames, false, max_step_ms)};
}

MapOptions Document::BakeOptions() const {
    MapOptions opts = RecipeFor(profile);
    const DeviceRecipe& r = recipe;
    if (!r.sync.empty()) {
        try {
            opts.sync = ParseSyncMode(r.sync);
        } catch (const std::invalid_argument&) {
        }
    }
    if (r.tick_ms > 0) opts.tick_ms = r.tick_ms;
    if (r.max_speed > 0) opts.max_speed = r.max_speed;
    opts.smoothing = 0;  // bake without recipe smoothing blur
    opts.min_suction = r.min_suction;
    opts.contact_vibration = r.contact_vibration;
    opts.contact_vibration_span = r.contact_vibration_span;
    opts.contact_vibration_curve = r.contact_vibration_curve;
    opts.scene_feel = NormalizeSceneFeel(r.scene_feel);
    opts.scene_ranges = NormalizeSceneRanges(r.scene_ranges);
    opts.vibration_follows_suction = r.vibration_follows_suction;
    opts.balance = r.feel_balance;
    opts.tracking_gaps = tracking_gaps;
    opts.contact_vibration_envelope = -1;
    return opts;
}

void Document::BakeNeoAxes() {
    Normalize();
    auto axes = BakeAxesFromRecipe(general, BakeOptions(), 100);
    vibration = std::move(axes.first);
    suction = std::move(axes.second);
    playback_source = PlaybackAxes;
    recipe.playback_source = PlaybackAxes;
}

const StrengthPreset* Document::ActivePreset() const {
    if (active_strength.empty()) return nullptr;
    for (const auto& p : strength_presets)
        if (p.name == active_strength) return &p;
    return nullptr;
}

void Document::ApplyContactRecipe(bool enabled, double span, const std::string& curve) {
    if (recipe.sync.empty()) recipe.sync = DefaultSyncForProfile(profile);
    recipe.contact_vibration = enabled;
    if (enabled) {
        recipe.contact_vibration_span = EffectiveContactSpan(span);
        recipe.contact_vibration_curve = NormalizeContactCurve(curve);
    } else {
        recipe.contact_vibration_span = 0;
        recipe.contact_vibration_curve.clear();
    }
}

void Document::ApplyFeelRecipe(const std::string& scene_feel, bool follow_suction, double balance) {
    if (recipe.sync.empty()) recipe.sync = DefaultSyncForProfile(profile);
    recipe.scene_feel = NormalizeSceneFeel(scene_feel);
    recipe.vibration_follows_suction = follow_suction;
    recipe.feel_balance = ClampBalance(balance);
}

std::vector<StrengthPreset> DefaultStrengthPresets() {
    std::vector<StrengthPreset> v(3);
    v[0].name = "soft";
    v[0].vibration_scale = 0.7;
    v[0].suction_scale = 0.85;
    v[0].contact_span = 0.85;
    v[0].contact_curve = "soft";
    v[1].name = "normal";
    v[1].vibration_scale = 1.0;
    v[1].suction_scale = 1.0;
    v[2].name = "strong";
    v[2].vibration_scale = 1.3;
    v[2].suction_scale = 1.1;
    v[2].contact_span = 0.55;
    v[2].contact_curve = "peak";
    return v;
}

}  // namespace samn
