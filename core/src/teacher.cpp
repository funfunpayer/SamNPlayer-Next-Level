#include "samn/teacher.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <set>
#include <tuple>

namespace samn {

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Every top-level {...} block in the text that parses as JSON, in order.
std::vector<nlohmann::json> JsonBlocks(const std::string& text) {
    std::vector<nlohmann::json> out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{' && text[i] != '[') continue;
        int depth = 0;
        bool in_string = false;
        for (size_t j = i; j < text.size(); ++j) {
            const char c = text[j];
            if (in_string) {
                if (c == '\\') ++j;
                else if (c == '"') in_string = false;
                continue;
            }
            if (c == '"') in_string = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0) {
                    try {
                        out.push_back(nlohmann::json::parse(text.substr(i, j - i + 1)));
                        i = j;
                    } catch (const nlohmann::json::exception&) {
                    }
                    break;
                }
            }
        }
    }
    return out;
}

std::optional<LabelBox> BoxOf(const nlohmann::json& o, double w, double h) {
    const auto nums = [](const nlohmann::json& a) -> std::optional<std::vector<double>> {
        if (!a.is_array() || a.size() != 4) return std::nullopt;
        std::vector<double> v;
        for (const auto& x : a) {
            if (!x.is_number()) return std::nullopt;
            v.push_back(x.get<double>());
        }
        return v;
    };
    std::optional<std::vector<double>> v;
    bool corners = false;
    if (o.contains("box")) v = nums(o["box"]);
    for (const char* key : {"bbox_2d", "bbox"})
        if (!v && o.contains(key)) {
            v = nums(o[key]);
            corners = true;
        }
    if (!v) return std::nullopt;
    auto [a, b, c, d] = std::tuple{(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
    if (corners) {
        c -= a;
        d -= b;
    }
    const double top = std::max({a, b, a + c, b + d});
    double sx = 1, sy = 1;
    if (top <= 1.0 + 1e-9) {
        sx = w;
        sy = h;
    } else if (a + c > w + 1 || b + d > h + 1) {
        sx = w / 1000.0;  // 0..1000
        sy = h / 1000.0;
    }
    LabelBox box{a * sx, b * sy, c * sx, d * sy};
    // Clip to the picture.
    const double x1 = std::clamp(box.x + box.w, 0.0, w), y1 = std::clamp(box.y + box.h, 0.0, h);
    box.x = std::clamp(box.x, 0.0, w);
    box.y = std::clamp(box.y, 0.0, h);
    box.w = x1 - box.x;
    box.h = y1 - box.y;
    if (box.w < 2 || box.h < 2) return std::nullopt;
    return box;
}

}  // namespace

std::string TeacherPrompt(const std::vector<std::string>& classes) {
    std::string list;
    for (const auto& c : classes) list += (list.empty() ? "" : ", ") + c;
    return "Find every visible instance of these classes in the picture: " + list +
           ". Answer with JSON only, no prose: {\"objects\": [{\"label\": \"<class>\", \"box\": [x, y, w, h]}]} "
           "where x, y is the top-left corner and w, h the size, all as fractions of the picture width and height "
           "(0..1). One entry per instance (two hands are two entries). Leave out what you cannot see. "
           "If nothing is visible, answer {\"objects\": []}.";
}

std::string TeacherRequestBody(const std::string& model, const std::string& prompt, const std::string& jpeg_base64) {
    nlohmann::json j;
    j["model"] = model;
    j["temperature"] = 0;
    j["stream"] = false;
    j["messages"] = nlohmann::json::array(
        {{{"role", "user"},
          {"content",
           nlohmann::json::array({{{"type", "text"}, {"text", prompt}},
                                  {{"type", "image_url"},
                                   {"image_url", {{"url", "data:image/jpeg;base64," + jpeg_base64}}}}})}}});
    return j.dump();
}

std::optional<std::string> ChatContent(const std::string& response_json, std::string* error) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(response_json);
    } catch (const nlohmann::json::exception&) {
        if (error) *error = "the server's answer is not JSON";
        return std::nullopt;
    }
    if (j.is_object() && j.contains("error")) {
        const auto& e = j["error"];
        if (error) *error = e.is_object() ? e.value("message", std::string("server error")) : e.dump();
        return std::nullopt;
    }
    try {
        return j.at("choices").at(0).at("message").at("content").get<std::string>();
    } catch (const nlohmann::json::exception&) {
        if (error) *error = "the server's answer has no message";
        return std::nullopt;
    }
}

std::vector<std::string> ParseModelList(const std::string& response_json) {
    std::vector<std::string> out;
    try {
        const auto j = nlohmann::json::parse(response_json);
        for (const auto& m : j.value("data", nlohmann::json::array()))
            if (m.is_object() && m.contains("id") && m["id"].is_string()) out.push_back(m["id"].get<std::string>());
    } catch (const nlohmann::json::exception&) {
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<TeacherBox> ParseTeacherBoxes(const std::string& text, double frame_w, double frame_h,
                                          const std::vector<std::string>& classes) {
    std::set<std::string> known;
    for (const auto& c : classes) known.insert(Lower(c));
    std::vector<TeacherBox> out;
    for (const auto& block : JsonBlocks(text)) {
        nlohmann::json list = block;
        if (block.is_object()) list = block.value("objects", block.value("detections", nlohmann::json::array()));
        if (!list.is_array()) continue;
        for (const auto& o : list) {
            if (!o.is_object()) continue;
            std::string cls;
            for (const char* key : {"label", "class", "name"})
                if (cls.empty() && o.contains(key) && o[key].is_string()) cls = o[key].get<std::string>();
            cls = LabelClassOf(Lower(cls));
            if (!known.count(cls)) continue;
            if (auto box = BoxOf(o, frame_w, frame_h)) out.push_back({cls, *box});
        }
        if (!out.empty()) break;  // the first block with boxes is the answer
    }
    return out;
}

int ApplyTeacherBoxes(LabelProject& p, int64_t ms, const std::vector<TeacherBox>& boxes, const std::string& source,
                      int64_t tolerance_ms) {
    std::set<std::string> used;
    int set = 0;
    for (const auto& b : boxes) {
        LabelTrack* best = nullptr;
        double best_iou = 0.1;
        for (auto& t : p.tracks) {
            if (t.cls() != b.cls || used.count(t.label())) continue;
            const LabelState s = t.At(ms);
            if (!s.present) continue;
            const double iou = BoxIoU(s.box, b.box);
            if (iou > best_iou) {
                best = &t;
                best_iou = iou;
            }
        }
        if (!best) {
            // A free object of the class without a box here, else a new one.
            for (auto& t : p.tracks)
                if (t.cls() == b.cls && !used.count(t.label()) && !t.At(ms).present && !t.KeyAt(ms, tolerance_ms)) {
                    best = &t;
                    break;
                }
        }
        if (!best) best = &p.AddObject(b.cls);
        used.insert(best->label());
        if (const LabelKey* k = best->KeyAt(ms, tolerance_ms); k && k->confirmed) continue;  // the user wins
        LabelKey key;
        key.ms = ms;
        key.box = b.box;
        key.confirmed = false;
        key.source = source;
        key.uncertain = b.uncertain;
        best->SetKey(key, tolerance_ms);
        ++set;
    }
    return set;
}

}  // namespace samn
