#include "samn/range_edit.h"

#include <cmath>
#include <utility>

namespace samn {

int64_t SnapMs(int64_t t, double fps) {
    if (fps <= 0 || std::isnan(fps) || std::isinf(fps)) return t;
    const double frame = std::round(static_cast<double>(t) * fps / 1000.0);
    return static_cast<int64_t>(std::round(frame * 1000.0 / fps));
}

std::vector<Action> DeleteRange(const std::vector<Action>& actions, int64_t start, int64_t end) {
    if (end < start) std::swap(start, end);
    std::vector<Action> out;
    out.reserve(actions.size());
    for (const Action& a : actions)
        if (a.at < start || a.at > end) out.push_back(a);
    if (out.size() < 2) throw RangeEditError("funscript: deleting would leave fewer than 2 points");
    return out;
}

std::vector<Action> CapSpeedRange(const std::vector<Action>& actions, int64_t start, int64_t end,
                                  double max_intensity) {
    if (max_intensity <= 0) max_intensity = DefaultMaxIntensity;
    if (actions.size() < 2) throw RangeEditError("funscript: too few points");
    if (end < start) std::swap(start, end);
    std::vector<Action> out = actions;
    // Overlap uses the original timestamps: after the first stretch the shifted
    // times would pass `end` and skip later in-range segments.
    std::vector<int64_t> orig_at(out.size());
    for (size_t i = 0; i < out.size(); ++i) orig_at[i] = out[i].at;
    int64_t shift = 0;
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        out[i].at = orig_at[i] + shift;
        const int64_t next_at = orig_at[i + 1] + shift;
        const double dt = static_cast<double>(next_at - out[i].at);
        if (dt <= 0) continue;
        if (orig_at[i + 1] < start || orig_at[i] > end) continue;
        const double dpos = std::fabs(static_cast<double>(out[i + 1].pos - out[i].pos));
        const double inten = 500.0 * dpos / dt;
        if (inten <= max_intensity) continue;
        const double need = 500.0 * dpos / max_intensity;
        const int64_t extra = static_cast<int64_t>(std::ceil(need - dt));
        if (extra > 0) shift += extra;
    }
    out.back().at = orig_at.back() + shift;
    return out;
}

std::vector<Action> ScaleRangePos(const std::vector<Action>& actions, int64_t start, int64_t end,
                                  double factor) {
    return ScaleRangePosFade(actions, start, end, factor, false);
}

std::vector<Action> ScaleRangePosFade(const std::vector<Action>& actions, int64_t start, int64_t end,
                                      double factor, bool soft_edges) {
    if (end < start) std::swap(start, end);
    const int64_t span = end - start;
    std::vector<Action> out = actions;
    for (Action& a : out) {
        if (a.at < start || a.at > end) continue;
        double f = factor;
        if (soft_edges && span > 0) {
            // weight 0 at the edges, 1 at the midpoint
            const double mid = static_cast<double>(start + end) / 2;
            const double half = static_cast<double>(span) / 2;
            double w = 1 - std::fabs(static_cast<double>(a.at) - mid) / half;
            if (w < 0) w = 0;
            f = 1 + (factor - 1) * w;
        }
        double p = 50 + (static_cast<double>(a.pos) - 50) * f;
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        a.pos = static_cast<int>(std::round(p));
    }
    return out;
}

}  // namespace samn
