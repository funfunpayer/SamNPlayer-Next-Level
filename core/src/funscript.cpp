#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/number.h"
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
        // The locale-free read, with std::stod's rules: a number prefix counts,
        // nothing readable or out of range is an error.
        const ParsedDouble n = ParseDoublePrefix(j.get<std::string>());
        if (!n.ok || n.out_of_range) throw std::runtime_error("non-number");
        const double f = std::max(-1e15, std::min(1e15, n.value));
        return static_cast<int64_t>(std::round(f));
    }
    throw std::runtime_error("non-number");
}

// MetaDuration: the shared flexible time (marks.h); unreadable text and a
// value that is no time at all count as "not set".
int64_t meta_duration_ms(const json& j) {
    try {
        const int64_t ms = FlexTimeMs(j);
        return ms > 0 && ms <= MaxActionMs ? ms : 0;
    } catch (const std::exception&) {
        return 0;
    }
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
            s.duration_meta = meta_duration_ms(meta["duration"]);
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
