#include "samn/sam_file.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>

#include "samn/fileio.h"
#include "samn/number.h"

namespace samn {

namespace {

namespace fs = std::filesystem;

// Streams the document: memory stays at the frames kept (16 bytes each), never
// a tree of the file.
class SamHandler {
public:
    SamFile file;
    std::string err;

    bool null() { return true; }  // null counts as missing
    bool boolean(bool) { return Scalar(Kind::Other, 0, nullptr); }
    bool number_integer(nlohmann::json::number_integer_t v) { return Scalar(Kind::Number, static_cast<double>(v), nullptr); }
    bool number_unsigned(nlohmann::json::number_unsigned_t v) {
        return Scalar(Kind::Number, static_cast<double>(v), nullptr);
    }
    bool number_float(nlohmann::json::number_float_t, const std::string& text) {
        // nlohmann writes the locale's decimal point into the token; read it
        // back with the locale-free helper.
        std::string t = text;
        std::replace(t.begin(), t.end(), ',', '.');
        const ParsedDouble p = ParseDoublePrefix(t);
        if (!p.ok || p.out_of_range || !std::isfinite(p.value)) return Fail("a number is out of range");
        return Scalar(Kind::Number, p.value, nullptr);
    }
    bool string(std::string& v) { return Scalar(Kind::String, 0, &v); }
    bool binary(nlohmann::json::binary_t&) { return Fail("binary data is not allowed"); }

    bool start_object(std::size_t) { return Open(false); }
    bool start_array(std::size_t) { return Open(true); }
    bool key(std::string& k) {
        if (!st_.empty()) st_.back().key = k;
        return true;
    }
    bool end_object() { return Close(); }
    bool end_array() { return Close(); }
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception& e) {
        if (err.empty()) err = std::string("not valid JSON: ") + e.what();
        return false;
    }

private:
    enum class Kind { Number, String, Other };
    struct Level {
        bool is_array = false;
        std::string key;      // object: the current key
        size_t index = 0;     // array: elements seen so far
    };
    std::vector<Level> st_;
    SamFrame cur_;

    bool Fail(const std::string& m) {
        if (err.empty()) err = m;
        return false;
    }

    // st_[0] is the root object; "frames" is st_[1] (array), a frame st_[2],
    // "motion" st_[3].
    bool InFrames() const { return st_.size() >= 2 && !st_[0].is_array && st_[0].key == "frames" && st_[1].is_array; }

    bool Open(bool is_array) {
        if (st_.size() >= kSamMaxDepth) return Fail("the file is nested too deeply");
        if (st_.empty()) {
            if (is_array) return Fail("the top level must be an object");
        } else {
            Level& top = st_.back();
            if (top.is_array) {
                if (InFrames() && st_.size() == 2) {
                    if (is_array) return Fail("frames[" + std::to_string(top.index) + "] is not an object");
                    cur_ = SamFrame{};
                }
                ++top.index;
            } else if (st_.size() == 1) {
                if (top.key == "version") return Fail("version must be text");
                if (top.key == "frames" && !is_array) return Fail("frames must be a list");
                if (top.key == "metadata" && is_array) return Fail("metadata must be an object");
            } else if (st_.size() == 3 && InFrames()) {
                const std::string where = "frames[" + std::to_string(st_[1].index - 1) + "].";
                if (top.key == "time") return Fail(where + "time must be a number");
                if (top.key == "motion" && is_array) return Fail(where + "motion must be an object");
            }
        }
        Level l;
        l.is_array = is_array;
        st_.push_back(std::move(l));
        return true;
    }

    bool Close() {
        // A frame object ends: st_ is root, frames, frame.
        if (st_.size() == 3 && InFrames() && !st_[2].is_array) {
            if (file.frames.size() >= kSamMaxFrames) return Fail("too many frames (limit 1000000)");
            if (cur_.time_ms < 0)
                ++file.dropped;
            else
                file.frames.push_back(cur_);
        }
        st_.pop_back();
        return true;
    }

    bool Scalar(Kind kind, double num, const std::string* text) {
        if (st_.empty()) return Fail("the top level must be an object");
        const Level& top = st_.back();
        if (top.is_array) {
            if (InFrames() && st_.size() == 2)
                return Fail("frames[" + std::to_string(top.index) + "] is not an object");
            return true;
        }
        const std::string& k = top.key;
        if (st_.size() == 1) {
            if (k == "version") {
                if (kind != Kind::String) return Fail("version must be text");
                file.version = *text;
            } else if (k == "frames") {
                return Fail("frames must be a list");
            } else if (k == "metadata") {
                return Fail("metadata must be an object");
            }
            return true;
        }
        if (st_.size() == 2 && st_[0].key == "metadata") {
            std::string* dst = k == "creator" ? &file.creator : k == "source" ? &file.source : k == "profile" ? &file.profile : nullptr;
            if (dst) {
                if (kind != Kind::String) return Fail("metadata." + k + " must be text");
                *dst = text->substr(0, 256);
            }
            return true;
        }
        if (InFrames() && st_.size() == 3 && !st_[2].is_array) {
            const std::string where = "frames[" + std::to_string(st_[1].index - 1) + "].";
            if (k == "time") {
                if (kind != Kind::Number) return Fail(where + "time must be a number");
                if (!(num > -9e15 && num < 9e15)) return Fail(where + "time is out of range");
                cur_.time_ms = static_cast<int64_t>(std::llround(num));
            } else if (k == "motion") {
                return Fail(where + "motion must be an object");
            }
            return true;
        }
        if (InFrames() && st_.size() == 4 && !st_[3].is_array && st_[2].key == "motion" && k == "intensity") {
            if (kind != Kind::Number)
                return Fail("frames[" + std::to_string(st_[1].index - 1) + "].motion.intensity must be a number");
            cur_.intensity = std::min(1.0, std::max(0.0, num));
        }
        return true;
    }
};

}  // namespace

SamReadResult ParseSam(std::string_view data) {
    SamReadResult r;
    if (data.size() > kSamMaxBytes) {
        r.error = "The .sam file is too large (limit 64 MB).";
        return r;
    }
    if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.remove_prefix(3);
    // nlohmann reads a NUL as the end of the input; text never holds one.
    if (data.find('\0') != std::string_view::npos) {
        r.error = "Invalid .sam file: it is binary (contains a NUL byte).";
        return r;
    }
    SamHandler h;
    bool ok = false;
    try {
        ok = nlohmann::json::sax_parse(data.begin(), data.end(), &h, nlohmann::json::input_format_t::json, true);
    } catch (const std::exception& e) {
        if (h.err.empty()) h.err = e.what();
    }
    if (!ok) {
        r.error = "Invalid .sam file: " + (h.err.empty() ? std::string("not valid JSON") : h.err) + ".";
        return r;
    }
    if (h.file.version.empty()) {
        r.error = "Invalid .sam file: the version is missing.";
        return r;
    }
    if (h.file.frames.empty()) {
        r.error = "Invalid .sam file: it has no frames.";
        return r;
    }
    std::stable_sort(h.file.frames.begin(), h.file.frames.end(),
                     [](const SamFrame& a, const SamFrame& b) { return a.time_ms < b.time_ms; });
    r.file = std::move(h.file);
    return r;
}

SamReadResult LoadSam(const fs::path& path) {
    SamReadResult r;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        r.error = "The .sam file could not be read.";
        return r;
    }
    if (size > kSamMaxBytes) {
        r.error = "The .sam file is too large (limit 64 MB).";
        return r;
    }
    const auto bytes = ReadFileToString(path);
    if (!bytes) {
        r.error = "The .sam file could not be read.";
        return r;
    }
    return ParseSam(*bytes);
}

std::string SamSidecarPath(const std::string& script_path) {
    if (script_path.empty()) return {};
    size_t dot = std::string::npos;
    for (size_t i = script_path.size(); i-- > 0;) {
        if (script_path[i] == '/') break;
        if (script_path[i] == '.') {
            dot = i;
            break;
        }
    }
    return (dot == std::string::npos ? script_path : script_path.substr(0, dot)) + ".sam";
}

bool SamHasContactIntensity(const SamFile& f) {
    for (const auto& fr : f.frames)
        if (fr.intensity > 0.001) return true;
    return false;
}

std::vector<Action> SamIntensityLane(const SamFile& f) {
    std::vector<Action> out;
    out.reserve(f.frames.size());
    for (const auto& fr : f.frames) {
        const Action a{fr.time_ms, static_cast<int>(std::lround(fr.intensity * 100.0))};
        if (!out.empty() && out.back().at == a.at)
            out.back() = a;
        else
            out.push_back(a);
    }
    return out;
}

}  // namespace samn
