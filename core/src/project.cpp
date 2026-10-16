#include "samn/project.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "samn/fileio.h"

namespace samn {

namespace {

using nlohmann::ordered_json;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Go decodes into a struct: keys match case-insensitively, the last one wins,
// null leaves the field alone, a wrong type is an error.
const ordered_json* Field(const ordered_json& obj, const char* name) {
    const ordered_json* found = nullptr;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (Lower(it.key()) == Lower(name) && !it.value().is_null()) found = &it.value();
    }
    return found;
}

[[noreturn]] void Bad(const std::string& what) { throw std::invalid_argument(what); }

std::string AsString(const ordered_json& v, const char* key) {
    if (!v.is_string()) Bad(std::string("Feld ") + key + " ist kein Text");
    return v.get<std::string>();
}

int64_t AsInt(const ordered_json& v, const char* key) {
    if (!v.is_number_integer()) Bad(std::string("Feld ") + key + " ist keine ganze Zahl");
    if (v.is_number_unsigned() && v.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX))
        Bad(std::string("Feld ") + key + " ist zu groß");
    return v.get<int64_t>();
}

const ordered_json& AsObject(const ordered_json& v, const char* key) {
    if (!v.is_object()) Bad(std::string("Feld ") + key + " ist kein Objekt");
    return v;
}

template <class T, class F>
std::vector<T> AsArray(const ordered_json& v, const char* key, F&& decode) {
    if (!v.is_array()) Bad(std::string("Feld ") + key + " ist kein Array");
    std::vector<T> out;
    for (const auto& item : v) {
        T t{};
        if (!item.is_null()) decode(AsObject(item, key), t);
        out.push_back(std::move(t));
    }
    return out;
}

std::string Num(int64_t v) { return std::to_string(v); }

}  // namespace

std::filesystem::path ProjectPathFor(const std::filesystem::path& media_path) {
    const std::string s = media_path.string();
    size_t dot = std::string::npos;
    for (size_t i = s.size(); i-- > 0;) {
        if (s[i] == '/' || s[i] == std::filesystem::path::preferred_separator) break;
        if (s[i] == '.') {
            dot = i;
            break;
        }
    }
    return std::filesystem::path((dot == std::string::npos ? s : s.substr(0, dot)) + ".snp.json");
}

std::string ProjectToJson(const Project& p) {
    // Compact Go json.Marshal text, then Go's json.Indent: same bytes as MarshalIndent.
    std::string j = "{\"version\":" + Num(p.version == 0 ? ProjectVersion : p.version);
    if (!p.video_path.empty()) j += ",\"videoPath\":" + GoJsonString(p.video_path);
    if (!p.script_path.empty()) j += ",\"scriptPath\":" + GoJsonString(p.script_path);
    if (p.offset_ms != 0) j += ",\"offsetMs\":" + Num(p.offset_ms);
    if (p.seek_ms != 0) j += ",\"seekMs\":" + Num(p.seek_ms);
    if (p.loop_marker)
        j += ",\"loopMarker\":{\"startMs\":" + Num(p.loop_marker->start_ms) +
             ",\"endMs\":" + Num(p.loop_marker->end_ms) + "}";
    if (!p.bookmarks.empty()) {
        j += ",\"bookmarks\":[";
        for (size_t i = 0; i < p.bookmarks.size(); ++i) {
            if (i) j += ",";
            j += "{\"name\":" + GoJsonString(p.bookmarks[i].name) + ",\"time\":" + Num(p.bookmarks[i].time_ms) + "}";
        }
        j += "]";
    }
    if (!p.chapters.empty()) {
        j += ",\"chapters\":[";
        for (size_t i = 0; i < p.chapters.size(); ++i) {
            const Chapter& c = p.chapters[i];
            if (i) j += ",";
            j += "{\"name\":" + GoJsonString(c.name) + ",\"startTime\":" + Num(c.start_ms);
            if (c.end_ms != 0) j += ",\"endTime\":" + Num(c.end_ms);
            j += "}";
        }
        j += "]";
    }
    if (p.last_opened_ms != 0) j += ",\"lastOpenedMs\":" + Num(p.last_opened_ms);
    j += "}";
    return GoIndent(j);
}

void SaveProject(const std::filesystem::path& path, const Project& p) {
    WriteFileAtomic(path, ProjectToJson(p));
}

Project LoadProject(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ProjectError("open " + path.string() + ": cannot read file");
    std::stringstream ss;
    ss << in.rdbuf();

    Project p;
    try {
        const ordered_json root = ordered_json::parse(ss.str());
        if (!root.is_null()) {  // Go: JSON null leaves the zero value
            AsObject(root, "project");
            if (auto* v = Field(root, "version")) p.version = static_cast<int>(AsInt(*v, "version"));
            if (auto* v = Field(root, "videoPath")) p.video_path = AsString(*v, "videoPath");
            if (auto* v = Field(root, "scriptPath")) p.script_path = AsString(*v, "scriptPath");
            if (auto* v = Field(root, "offsetMs")) p.offset_ms = AsInt(*v, "offsetMs");
            if (auto* v = Field(root, "seekMs")) p.seek_ms = AsInt(*v, "seekMs");
            if (auto* v = Field(root, "lastOpenedMs")) p.last_opened_ms = AsInt(*v, "lastOpenedMs");
            if (auto* v = Field(root, "loopMarker")) {
                const auto& o = AsObject(*v, "loopMarker");
                RangeMarker m;
                if (auto* s = Field(o, "startMs")) m.start_ms = AsInt(*s, "startMs");
                if (auto* e = Field(o, "endMs")) m.end_ms = AsInt(*e, "endMs");
                p.loop_marker = m;
            }
            if (auto* v = Field(root, "bookmarks"))
                p.bookmarks = AsArray<Bookmark>(*v, "bookmarks", [](const ordered_json& o, Bookmark& b) {
                    if (auto* n = Field(o, "name")) b.name = AsString(*n, "name");
                    if (auto* t = Field(o, "time")) b.time_ms = AsInt(*t, "time");
                });
            if (auto* v = Field(root, "chapters"))
                p.chapters = AsArray<Chapter>(*v, "chapters", [](const ordered_json& o, Chapter& c) {
                    if (auto* n = Field(o, "name")) c.name = AsString(*n, "name");
                    if (auto* s = Field(o, "startTime")) c.start_ms = AsInt(*s, "startTime");
                    if (auto* e = Field(o, "endTime")) c.end_ms = AsInt(*e, "endTime");
                });
        }
    } catch (const std::exception& exc) {
        throw ProjectError(std::string("project: ungültiges JSON: ") + exc.what());
    }
    if (p.version == 0) p.version = ProjectVersion;
    return p;
}

}  // namespace samn
