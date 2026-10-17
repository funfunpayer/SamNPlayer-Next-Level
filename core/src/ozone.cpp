#include "samn/ozone.h"

#include <algorithm>
#include <cstdio>

namespace samn {

namespace {

// A secondary rise must stand 10 above the median of the earlier windows (a
// steadily raised but flat lead-in is no rise) and stay under 0.85 of the
// primary (else it would be a second primary).
constexpr double kSecondaryMinMargin = 10.0;
constexpr double kSecondaryCeilingFrac = 0.85;

double WindowMeanPos(const std::vector<Action>& actions, int64_t from, int64_t to) {
    double sum = 0;
    int n = 0;
    for (const Action& a : actions)
        if (a.at >= from && a.at <= to) {
            sum += a.pos;
            ++n;
        }
    return n ? sum / n : 0;
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t mid = v.size() / 2;
    return v.size() % 2 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
}

std::string Format(const char* fmt, double a, double b = 0) {
    char buf[160];
    std::snprintf(buf, sizeof buf, fmt, a, b);
    return buf;
}

}  // namespace

OZoneSuggestion SuggestOZone(const std::vector<Action>& actions) {
    OZoneSuggestion s;
    if (actions.size() < 4) {
        s.reason = "too few points";
        return s;
    }
    const int64_t start = actions.front().at, end = actions.back().at, dur = end - start;
    if (dur < 4000) {
        s.reason = "script shorter than 4 s: no O suggestion";
        return s;
    }
    int64_t tail = start + static_cast<int64_t>(dur * 0.85);
    const int64_t win = std::max<int64_t>(1500, static_cast<int64_t>(dur * 0.08));
    if (tail + win > end) tail = end - win;
    const int64_t step = std::max<int64_t>(200, win / 4);
    int64_t best_start = tail;
    double best_mean = -1;
    for (int64_t w0 = tail; w0 + win <= end; w0 += step) {
        const double mean = WindowMeanPos(actions, w0, w0 + win);
        if (mean > best_mean) {
            best_mean = mean;
            best_start = w0;
        }
    }
    if (best_mean < 35) {
        s.reason = Format("the last eighth stays flat (mean %.0f): no O suggestion", best_mean);
        return s;
    }
    s.start_ms = best_start;
    s.end_ms = best_start + win;
    s.ok = true;
    s.mean = best_mean;
    s.reason = Format("last eighth, window with the highest mean position (%.0f)", best_mean);
    return s;
}

std::vector<OZoneSuggestion> SuggestSecondaryOZones(const std::vector<Action>& actions,
                                                    const OZoneSuggestion& primary, int max_count) {
    std::vector<OZoneSuggestion> out;
    if (!primary.ok || max_count <= 0 || actions.size() < 4) return out;
    const int64_t start = actions.front().at, win = primary.end_ms - primary.start_ms;
    if (win <= 0 || primary.start_ms - start < win) return out;  // no room for a window before it
    double primary_mean = primary.mean > 0 ? primary.mean : WindowMeanPos(actions, primary.start_ms, primary.end_ms);
    if (primary_mean <= 0) return out;

    const int64_t step = std::max<int64_t>(200, win / 4);
    struct Window {
        int64_t start, end;
        double mean;
    };
    std::vector<Window> windows;
    std::vector<double> means;
    for (int64_t w0 = start; w0 + win <= primary.start_ms; w0 += step) {
        windows.push_back({w0, w0 + win, WindowMeanPos(actions, w0, w0 + win)});
        means.push_back(windows.back().mean);
    }
    if (windows.empty()) return out;
    const double baseline = Median(means);
    std::vector<Window> pool;
    for (const Window& w : windows)
        if (w.mean > baseline + kSecondaryMinMargin && w.mean < primary_mean * kSecondaryCeilingFrac) pool.push_back(w);
    std::stable_sort(pool.begin(), pool.end(), [](const Window& a, const Window& b) { return a.mean > b.mean; });
    for (const Window& c : pool) {
        if (static_cast<int>(out.size()) >= max_count) break;
        const bool overlaps = std::any_of(out.begin(), out.end(), [&](const OZoneSuggestion& o) {
            return c.start < o.end_ms && o.start_ms < c.end;
        });
        if (overlaps) continue;
        OZoneSuggestion s;
        s.start_ms = c.start;
        s.end_ms = c.end;
        s.ok = true;
        s.mean = c.mean;
        s.reason = Format("earlier, weaker rise (mean %.0f, primary %.0f)", c.mean, primary_mean);
        out.push_back(s);
    }
    // In time order once chosen; reads more naturally than by strength.
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start_ms < b.start_ms; });
    return out;
}

}  // namespace samn
