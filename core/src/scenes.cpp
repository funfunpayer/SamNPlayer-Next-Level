#include "samn/scenes.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <type_traits>

#include <nlohmann/json.hpp>


namespace samn {

namespace {

// JSON null or a wrong type reads as the fallback (0.7: null scene_type = unknown).
template <typename T>
T Field(const nlohmann::json& o, const char* key, T fallback) {
    const auto it = o.find(key);
    if (it == o.end()) return fallback;
    if constexpr (std::is_same_v<T, std::string>) {
        return it->is_string() ? it->template get<std::string>() : fallback;
    } else {
        return it->is_number() ? it->template get<T>() : fallback;
    }
}

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

}  // namespace

const std::vector<std::string>& SceneFeelNames() {
    static const std::vector<std::string> kNames{"", SceneFeelOral, SceneFeelIntercourse, SceneFeelBreast,
                                                 SceneFeelHand};
    return kNames;
}

std::filesystem::path SceneProposalsPath(const std::filesystem::path& video) {
    std::filesystem::path p = video;
    return p.replace_extension(".scene.json");
}

std::optional<std::vector<SceneProposal>> LoadSceneProposals(const std::filesystem::path& path, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<std::vector<SceneProposal>> {
        if (error) *error = "scene proposals " + path.u8string() + ": " + why;
        return std::nullopt;
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot be read");
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    if (!j.is_object()) return fail("not an object");
    try {
        if (Field(j, "version", 0) != 1) return fail("unsupported version");
        const int64_t w = Field(j, "width", int64_t{0}), h = Field(j, "height", int64_t{0});
        if (w <= 0 || h <= 0) return fail("missing width/height");
        std::vector<SceneProposal> out;
        if (j.contains("proposals") && j["proposals"].is_array()) {
            for (const auto& p : j["proposals"]) {
                if (!p.is_object() || !p.contains("primary") || !p["primary"].is_object()) continue;
                const auto& b = p["primary"];
                const int64_t x = Field(b, "x", int64_t{-1}), y = Field(b, "y", int64_t{-1});
                const int64_t bw = Field(b, "w", int64_t{0}), bh = Field(b, "h", int64_t{0});
                if (bw <= 0 || bh <= 0 || x < 0 || y < 0 || x + bw > w || y + bh > h) continue;
                SceneProposal s;
                s.t_ms = Field(p, "t_ms", int64_t{0});
                s.start_ms = Field(p, "start_ms", int64_t{0});
                s.end_ms = Field(p, "end_ms", int64_t{0});
                s.scene_type = Field(p, "scene_type", std::string());
                s.confidence = Field(p, "confidence", 0.0);
                out.push_back(std::move(s));
            }
        }
        std::stable_sort(out.begin(), out.end(), [](const SceneProposal& a, const SceneProposal& b) { return a.t_ms < b.t_ms; });
        return out;
    } catch (const nlohmann::json::exception& e) {
        return fail(e.what());
    }
}

std::string ContactClassScene(const std::string& cls) {
    const std::string c = Lower(cls);
    auto has = [&](const char* s) { return c.find(s) != std::string::npos; };
    if (has("mouth") || has("lips")) return SceneFeelOral;
    if (has("nipple") || has("breast")) return SceneFeelBreast;
    if (has("hand")) return SceneFeelHand;
    return "";
}

std::vector<ChapterScene> ChapterScenes(const std::vector<Chapter>& chapters, int64_t total_ms,
                                        const std::vector<SceneFeelRange>& saved,
                                        const std::vector<SceneProposal>& proposals,
                                        const std::optional<ContactMarks>& marks) {
    std::string mark_feel;
    if (marks && marks->primary) mark_feel = NormalizeSceneFeel(ContactClassScene(marks->primary->cls));
    std::vector<ChapterScene> out;
    for (const ChapterRange& r : ChapterRanges(chapters, total_ms)) {
        ChapterScene cs;
        cs.index = r.index;
        cs.name = r.name;
        cs.start_ms = r.start_ms;
        cs.end_ms = r.end_ms;
        for (const SceneFeelRange& s : saved)
            if (s.start_ms == cs.start_ms && s.end_ms == cs.end_ms) {
                cs.feel = s.feel;
                cs.has_feel = true;
            }
        // Scene detection: overlap x confidence per feel. Ordered map, so a
        // tie picks the same feel every time.
        std::map<std::string, double> weight;
        std::map<std::string, std::pair<double, std::string>> strongest;  // the source that gave most
        double total = 0;
        for (const SceneProposal& p : proposals) {
            const int64_t lo = std::max(p.start_ms, cs.start_ms), hi = std::min(p.end_ms, cs.end_ms);
            if (hi <= lo) continue;
            const double span = double(hi - lo);
            total += span;
            const std::string f = NormalizeSceneFeel(p.scene_type);
            if (f.empty()) continue;
            const double w = span * std::clamp(p.confidence, 0.0, 1.0);
            weight[f] += w;
            if (w > strongest[f].first) strongest[f] = {w, p.source};
        }
        std::string best;
        double best_w = 0;
        for (const auto& [f, w] : weight)
            if (w > best_w) {
                best = f;
                best_w = w;
            }
        if (!best.empty() && total > 0) {
            cs.suggested = best;
            cs.source = strongest[best].second.empty() ? "scene detection" : strongest[best].second;
            cs.confidence = best_w / total;
        } else if (!mark_feel.empty()) {
            cs.suggested = mark_feel;
            cs.source = "contact mark";
            cs.confidence = 0.5;
        }
        out.push_back(std::move(cs));
    }
    return out;
}

std::vector<SceneProposal> PhaseProposals(const std::vector<LabelPhase>& phases) {
    std::vector<SceneProposal> out;
    for (const auto& ph : phases) {
        if (NormalizeSceneFeel(ph.name).empty() || ph.end_ms <= ph.start_ms) continue;
        SceneProposal p;
        p.t_ms = (ph.start_ms + ph.end_ms) / 2;
        p.start_ms = ph.start_ms;
        p.end_ms = ph.end_ms;
        p.scene_type = ph.name;
        p.confidence = ph.confirmed ? 1.0 : 0.6;
        p.source = "phases";
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<Chapter> ChaptersFromPhases(std::vector<Chapter> chapters, const std::vector<LabelPhase>& phases,
                                        int64_t total_ms, int* added) {
    int n = 0;
    std::vector<LabelPhase> confirmed;
    for (const auto& ph : phases)
        if (ph.confirmed && ph.end_ms > ph.start_ms) confirmed.push_back(ph);
    for (size_t i = 0; i < confirmed.size(); ++i) {
        const LabelPhase& ph = confirmed[i];
        if (auto next = AddSceneMarker(chapters, ph.start_ms)) {
            chapters = std::move(*next);
            std::string title = ph.name;
            if (!title.empty()) title[0] = char(std::toupper(static_cast<unsigned char>(title[0])));
            for (auto& c : chapters)
                if (c.start_ms == ph.start_ms) c.name = title;
            ++n;
        }
        // A stretch without a phase follows: its own scene.
        const bool gap = i + 1 < confirmed.size() ? confirmed[i + 1].start_ms > ph.end_ms
                                                  : ph.end_ms < total_ms - kSceneMarkerMinGapMs;
        if (gap)
            if (auto next = AddSceneMarker(chapters, ph.end_ms)) {
                chapters = std::move(*next);
                ++n;
            }
    }
    if (added) *added = n;
    return chapters;
}

std::vector<SceneFeelRange> SceneRangesOf(const std::vector<ChapterScene>& scenes) {
    std::vector<SceneFeelRange> out;
    for (const ChapterScene& s : scenes)
        if (s.has_feel) out.push_back({s.start_ms, s.end_ms, s.feel});
    return out;
}

}  // namespace samn
