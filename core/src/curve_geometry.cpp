#include "samn/curve_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace samn {

namespace {

double Clamp100(double v) { return std::clamp(v, 0.0, 100.0); }

CurvePoint P(const Action& a) { return {static_cast<double>(a.at), static_cast<double>(a.pos)}; }

}  // namespace

std::vector<CubicSegment> CatmullRomBezier(const std::vector<Action>& actions, size_t first, size_t last) {
    std::vector<CubicSegment> out;
    if (actions.size() < 2 || first >= last || last >= actions.size()) return out;
    out.reserve(last - first);
    const size_t end = actions.size() - 1;
    for (size_t i = first; i < last; ++i) {
        const CurvePoint p0 = P(actions[i == 0 ? 0 : i - 1]);
        const CurvePoint p1 = P(actions[i]);
        const CurvePoint p2 = P(actions[i + 1]);
        const CurvePoint p3 = P(actions[std::min(end, i + 2)]);
        CubicSegment s;
        s.p1 = p1;
        s.p2 = p2;
        s.c1 = {p1.at + (p2.at - p0.at) / 6.0, Clamp100(p1.pos + (p2.pos - p0.pos) / 6.0)};
        s.c2 = {p2.at - (p3.at - p1.at) / 6.0, Clamp100(p2.pos - (p3.pos - p1.pos) / 6.0)};
        out.push_back(s);
    }
    return out;
}

std::pair<size_t, size_t> VisibleRange(const std::vector<Action>& actions, int64_t t0, int64_t t1) {
    if (actions.empty()) return {1, 0};
    auto byAt = [](const Action& a, int64_t t) { return a.at < t; };
    const auto lo = std::lower_bound(actions.begin(), actions.end(), t0, byAt);
    const auto hi = std::upper_bound(actions.begin(), actions.end(), t1,
                                     [](int64_t t, const Action& a) { return t < a.at; });
    size_t first = static_cast<size_t>(lo - actions.begin());
    size_t last = static_cast<size_t>(hi - actions.begin());  // one past
    if (first > 0) --first;
    if (last >= actions.size()) last = actions.size() - 1;
    if (first > last) first = last;
    return {first, last};
}

std::optional<double> PositionAt(const std::vector<Action>& actions, int64_t ms) {
    if (actions.empty()) return std::nullopt;
    if (ms <= actions.front().at) return actions.front().pos;
    if (ms >= actions.back().at) return actions.back().pos;
    const auto it = std::upper_bound(actions.begin(), actions.end(), ms,
                                     [](int64_t t, const Action& a) { return t < a.at; });
    const Action& b = *it;
    const Action& a = *(it - 1);
    if (b.at == a.at) return b.pos;
    const double f = static_cast<double>(ms - a.at) / static_cast<double>(b.at - a.at);
    return a.pos + (b.pos - a.pos) * f;
}

size_t CountInWindow(const std::vector<Action>& actions, int64_t t0, int64_t t1) {
    if (actions.empty() || t1 < t0) return 0;
    const auto lo = std::lower_bound(actions.begin(), actions.end(), t0,
                                     [](const Action& a, int64_t t) { return a.at < t; });
    const auto hi = std::upper_bound(actions.begin(), actions.end(), t1,
                                     [](int64_t t, const Action& a) { return t < a.at; });
    return static_cast<size_t>(hi - lo);
}

bool IsDense(size_t visible, double widthPx, double minGapPx) {
    return visible > 2 && widthPx / static_cast<double>(visible) < minGapPx;
}

double SpeedBinPx(size_t visible, double widthPx) {
    if (visible == 0) return 4.0;
    return std::max(4.0, 2.5 * (widthPx / static_cast<double>(visible)));
}

std::vector<CurvePoint> SpeedWave(const std::vector<Action>& actions, double t0, double t1, double widthPx,
                                  double binPx) {
    std::vector<CurvePoint> pts;
    if (actions.size() < 2 || t1 <= t0 || widthPx <= 0 || binPx <= 0) return pts;
    const double span = t1 - t0;
    auto xOf = [&](double ms) { return (ms - t0) / span * widthPx; };
    // Only the points that can land in a bin near the window.
    const double margin = binPx / widthPx * span;
    const auto [first, last] = VisibleRange(actions, static_cast<int64_t>(std::floor(t0 - margin)),
                                            static_cast<int64_t>(std::ceil(t1 + margin)));
    struct Bin { double d = 0, t = 0; };
    std::map<long long, Bin> bins;
    for (size_t i = std::max<size_t>(first, 1); i <= last; ++i) {
        const Action& a = actions[i - 1];
        const Action& b = actions[i];
        const double x = xOf(static_cast<double>(b.at));
        if (x < -binPx || x > widthPx + binPx || b.at <= a.at) continue;
        Bin& e = bins[static_cast<long long>(std::floor(x / binPx))];
        e.d += std::abs(b.pos - a.pos);
        e.t += static_cast<double>(b.at - a.at);
    }
    pts.reserve(bins.size());
    for (const auto& [k, e] : bins) {
        const double speed = e.d / std::max(1.0, e.t) * 1000.0;
        pts.push_back({(static_cast<double>(k) + 0.5) * binPx, std::min(100.0, speed / kSpeedWaveFull * 100.0)});
    }
    for (int n = 0; n < 2; ++n) {
        std::vector<CurvePoint> next = pts;
        for (size_t i = 0; i < pts.size(); ++i) {
            const double prev = i > 0 ? pts[i - 1].pos : pts[i].pos;
            const double nxt = i + 1 < pts.size() ? pts[i + 1].pos : pts[i].pos;
            next[i].pos = (pts[i].pos * 2 + prev + nxt) / 4.0;
        }
        pts = std::move(next);
    }
    return pts;
}

ViewWindow FullView(int64_t totalMs) { return {0, std::max<int64_t>(1, totalMs)}; }

ViewWindow ClampView(ViewWindow v, int64_t totalMs) {
    const int64_t dur = std::max<int64_t>(1, totalMs);
    if (v.end_ms <= v.start_ms) return FullView(dur);
    v.start_ms = std::max<int64_t>(0, std::min(v.start_ms, dur - 1));
    v.end_ms = std::max(v.start_ms + 1, std::min(v.end_ms, dur));
    return v;
}

bool IsZoomed(ViewWindow v, int64_t totalMs) {
    return totalMs > 0 && (v.start_ms > 0 || v.end_ms < totalMs - 1);
}

ViewWindow ZoomView(ViewWindow v, int64_t totalMs, double factor, std::optional<double> anchorMs) {
    if (totalMs <= 0 || factor <= 0) return FullView(totalMs);
    v = ClampView(v, totalMs);
    const double span = static_cast<double>(v.end_ms - v.start_ms);
    const double total = static_cast<double>(totalMs);
    const double next = std::max(static_cast<double>(kMinViewSpanMs), std::min(total, span / factor));
    if (next >= total - 1) return FullView(totalMs);
    const double anchor = anchorMs.value_or((v.start_ms + v.end_ms) / 2.0);
    const double rel = span > 0 ? (anchor - static_cast<double>(v.start_ms)) / span : 0.5;
    auto start = static_cast<int64_t>(std::llround(anchor - rel * next));
    int64_t end = static_cast<int64_t>(std::llround(static_cast<double>(start) + next));
    if (start < 0) {
        end -= start;
        start = 0;
    }
    if (end > totalMs) {
        start -= end - totalMs;
        end = totalMs;
    }
    return ClampView({std::max<int64_t>(0, start), std::min(totalMs, end)}, totalMs);
}

ViewWindow ZoomToRange(int64_t startMs, int64_t endMs, int64_t totalMs) {
    if (endMs <= startMs) return ClampView(FullView(totalMs), totalMs);
    const int64_t pad = std::max<int64_t>(200, std::llround(static_cast<double>(endMs - startMs) * 0.08));
    return ClampView({std::max<int64_t>(0, startMs - pad), std::min(totalMs, endMs + pad)}, totalMs);
}

}  // namespace samn
