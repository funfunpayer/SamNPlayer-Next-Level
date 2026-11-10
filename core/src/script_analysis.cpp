#include "samn/script_analysis.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace samn {
namespace {

std::string Whole(double v) { return std::to_string(static_cast<long long>(std::llround(v))); }

const char* StateLabel(const std::string& state) {
    if (state == "static") return "still";
    if (state == "starting") return "starting";
    if (state == "accelerating") return "accelerating";
    if (state == "regular") return "steady";
    if (state == "decelerating") return "slowing";
    if (state == "stopping") return "stopping";
    return nullptr;
}

}  // namespace

std::string SummarizeMotionStates(const std::map<std::string, double>& shares, double total_ms) {
    if (shares.empty()) return "No segments detected.";
    struct Entry {
        std::string state;
        double share = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(shares.size());
    for (const auto& item : shares) entries.push_back(Entry{item.first, item.second});
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.share != b.share) return a.share > b.share;
        return a.state < b.state;
    });

    std::vector<std::string> parts;
    for (const auto& e : entries) {
        if (e.share < 0.02) continue;
        const char* known = StateLabel(e.state);
        const std::string label = known ? known : e.state;
        parts.push_back(label + " " + Whole(e.share * 100.0) + "%");
        if (parts.size() == 4) break;
    }

    std::string text = Whole(total_ms / 1000.0) + " seconds: ";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) text += ", ";
        text += parts[i];
    }
    const auto still = shares.find("static");
    if (still != shares.end() && still->second > 0.3) {
        // U+2014, as in 0.7 summarizeStates.
        text += " \u2014 more than " + Whole(still->second * 100.0) + "% still; long stretches without motion";
    }
    return text;
}

std::string MotionChapterLabel(const std::string& kind) {
    if (kind == "pause") return "Pause";
    if (kind == "build") return "Build-up";
    if (kind == "steady") return "steady";
    if (kind == "crescendo") return "Crescendo";
    if (kind == "winddown") return "Wind-down";
    return kind;
}

std::string FormatAnalysisClock(double ms) {
    long long seconds = std::llround(ms / 1000.0);
    if (seconds < 0) seconds = 0;
    const long long minutes = seconds / 60;
    const long long rest = seconds % 60;
    std::string out = std::to_string(minutes);
    out += ':';
    if (rest < 10) out += '0';
    out += std::to_string(rest);
    return out;
}

ScriptAnalysis AnalyzeScriptActions(const std::vector<Action>& actions) {
    ScriptAnalysis out;
    if (actions.size() < 4) {
        out.error = "no script loaded (or too few points)";
        return out;
    }
    std::vector<Action> ordered = actions;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Action& a, const Action& b) { return a.at < b.at; });
    std::vector<MotionPoint> points;
    points.reserve(ordered.size());
    for (const auto& a : ordered) points.push_back(MotionPoint{static_cast<double>(a.at), static_cast<double>(a.pos)});

    // The wrapper sets SmoothRadius 2. ClassifyOptions{} would leave it at 0.
    ClassifyOptions opt;
    opt.static_speed = 4;
    opt.change_ratio = 0.25;
    opt.min_segment_ms = 250;
    opt.smooth_radius = 2;

    out.segments = ClassifyMotion(points, opt);
    out.total_ms = points.back().t_ms - points.front().t_ms;
    for (const auto& seg : out.segments)
        out.share_by_state[MotionStateName(seg.state)] += seg.Duration();
    if (out.total_ms > 0) {
        for (auto& item : out.share_by_state) item.second /= out.total_ms;
    }
    out.summary = SummarizeMotionStates(out.share_by_state, out.total_ms);
    return out;
}

}  // namespace samn
