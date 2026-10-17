#include "samn/funscript.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace samn {

namespace {

using json = nlohmann::json;

int64_t round_number(const json& j) {
    if (j.is_null() || j.is_discarded()) return 0;
    if (j.is_number_integer()) return j.get<int64_t>();
    if (j.is_number_float()) {
        double f = j.get<double>();
        f = std::max(-1e15, std::min(1e15, f));
        return static_cast<int64_t>(std::round(f));
    }
    if (j.is_string()) {
        try {
            double f = std::stod(j.get<std::string>());
            f = std::max(-1e15, std::min(1e15, f));
            return static_cast<int64_t>(std::round(f));
        } catch (...) {
            throw std::runtime_error("non-number");
        }
    }
    throw std::runtime_error("non-number");
}

int64_t parse_clock_ms(const std::string& s) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ':')) {
        if (!part.empty()) parts.push_back(part);
    }
    if (parts.size() < 2 || parts.size() > 3) return -1;
    double total = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        double v;
        try {
            v = std::stod(parts[i]);
        } catch (...) {
            return -1;
        }
        if (v < 0 || std::isnan(v) || std::isinf(v)) return -1;
        if (i < parts.size() - 1 && v != std::floor(v)) return -1;
        total = total * 60 + v;
    }
    return static_cast<int64_t>(std::round(total * 1000));
}

int64_t normalize_time_to_ms(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    double absv = std::abs(v);
    double intpart;
    double frac = std::modf(absv, &intpart);
    if (absv < 100000.0 && frac > 1e-9) {
        return static_cast<int64_t>(std::round(v * 1000));
    }
    return static_cast<int64_t>(std::round(v));
}

int64_t flex_time_ms(const json& j) {
    if (j.is_null()) return 0;
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        // std::stod stops at the first non-digit ("10:31" -> 10), so only a
        // string that is a number as a whole counts as one.
        try {
            size_t used = 0;
            double f = std::stod(s, &used);
            if (used == s.size()) return normalize_time_to_ms(f);
        } catch (...) {
        }
        int64_t ms = parse_clock_ms(s);
        if (ms >= 0) return ms;
        return 0;  // unreadable -> 0 for MetaDuration
    }
    if (j.is_number()) {
        return normalize_time_to_ms(j.get<double>());
    }
    return 0;
}

}  // namespace

Script Parse(const std::string& json_str) {
    json doc;
    try {
        doc = json::parse(json_str);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("funscript: invalid JSON: ") + e.what());
    }
    if (!doc.is_object()) {
        throw std::runtime_error("funscript: invalid JSON: not an object");
    }

    Script s;
    if (doc.contains("inverted") && doc["inverted"].is_boolean()) {
        s.inverted = doc["inverted"].get<bool>();
    }

    if (!doc.contains("actions") || !doc["actions"].is_array()) {
        throw std::runtime_error("funscript: no actions found in the script");
    }
    const auto& arr = doc["actions"];
    if (arr.empty()) {
        throw std::runtime_error("funscript: no actions found in the script");
    }

    std::vector<Action> kept;
    int dropped = 0;
    for (const auto& item : arr) {
        if (!item.is_object()) continue;
        int64_t at = 0;
        int pos = 0;
        try {
            if (item.contains("at")) at = round_number(item["at"]);
            if (item.contains("pos")) pos = static_cast<int>(round_number(item["pos"]));
        } catch (...) {
            throw std::runtime_error("funscript: invalid JSON: bad action number");
        }
        if (at >= 0 && at <= MaxActionMs) {
            if (pos < 0) pos = 0;
            else if (pos > 100) pos = 100;
            kept.push_back({at, pos});
        } else {
            ++dropped;
        }
    }
    if (kept.empty()) {
        throw std::runtime_error("funscript: no valid actions (all times outside 0–" + std::to_string(MaxActionMs) + " ms)");
    }
    std::stable_sort(kept.begin(), kept.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
    s.actions = std::move(kept);
    s.dropped = dropped;

    // Duration
    if (doc.contains("metadata") && doc["metadata"].is_object()) {
        const auto& meta = doc["metadata"];
        if (meta.contains("duration")) {
            s.duration_meta = flex_time_ms(meta["duration"]);
        }
    }
    int64_t last = s.actions.empty() ? 0 : s.actions.back().at;
    if (s.duration_meta > 0 && s.duration_meta * 100 < last) {
        s.duration_meta *= 1000;
    }

    return s;
}

std::string Write(const Script& s) {
    json doc = json::object();
    json acts = json::array();
    for (const auto& a : s.actions) {
        acts.push_back({{"at", a.at}, {"pos", a.pos}});
    }
    doc["actions"] = acts;
    if (s.inverted) doc["inverted"] = true;
    if (s.duration_meta > 0) {
        doc["metadata"] = json::object({{"duration", s.duration_meta}});
    }
    return doc.dump(2);
}

std::pair<std::vector<Action>, int> ShiftActions(const std::vector<Action>& actions, int64_t deltaMs) {
    std::vector<Action> out;
    int dropped = 0;
    out.reserve(actions.size());
    for (const auto& a : actions) {
        int64_t at = a.at + deltaMs;
        if (at < 0) {
            ++dropped;
            continue;
        }
        out.push_back({at, a.pos});
    }
    return {std::move(out), dropped};
}

}  // namespace samn
