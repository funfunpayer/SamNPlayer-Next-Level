#include "samn/motion_classify.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace samn {
namespace {

void ApplyDefaults(ClassifyOptions& o) {
    // 0.7 applyDefaults. SmoothRadius is left at 0; only a negative value
    // is clamped, and nothing here writes 2.
    if (o.static_speed <= 0) o.static_speed = 4;
    if (o.change_ratio <= 0) o.change_ratio = 0.25;
    if (o.min_segment_ms <= 0) o.min_segment_ms = 250;
    if (o.smooth_radius < 0) o.smooth_radius = 0;
}

// |dPos/dt| per second, one value per sample. A non-positive step repeats
// the previous speed. The first sample copies the second.
std::vector<double> AbsSpeed(const std::vector<MotionPoint>& pts) {
    std::vector<double> out(pts.size(), 0);
    for (size_t i = 1; i < pts.size(); ++i) {
        const double dt = (pts[i].t_ms - pts[i - 1].t_ms) / 1000.0;
        if (dt <= 0) {
            out[i] = out[i - 1];
            continue;
        }
        out[i] = std::abs(pts[i].pos - pts[i - 1].pos) / dt;
    }
    if (out.size() > 1) out[0] = out[1];
    return out;
}

std::vector<double> MovingAverage(const std::vector<double>& v, int radius) {
    if (radius <= 0 || v.size() < 3) return v;
    std::vector<double> out(v.size(), 0);
    const int n = static_cast<int>(v.size());
    for (int i = 0; i < n; ++i) {
        int lo = i - radius;
        int hi = i + radius;
        if (lo < 0) lo = 0;
        if (hi >= n) hi = n - 1;
        double sum = 0;
        for (int j = lo; j <= hi; ++j) sum += v[static_cast<size_t>(j)];
        out[static_cast<size_t>(i)] = sum / static_cast<double>(hi - lo + 1);
    }
    return out;
}

MotionState ClassifyAt(const std::vector<double>& speed, int i, const ClassifyOptions& opt) {
    const double v = speed[static_cast<size_t>(i)];
    if (i == 0) return v <= opt.static_speed ? MotionState::Static : MotionState::Starting;
    const double prev = speed[static_cast<size_t>(i - 1)];
    if (v <= opt.static_speed) {
        return prev > opt.static_speed ? MotionState::Stopping : MotionState::Static;
    }
    if (prev <= opt.static_speed) return MotionState::Starting;
    // Both above the floor: a relative change, so a slow and a fast passage
    // are judged the same way.
    const double ref = std::max(prev, 1e-9);
    const double change = (v - prev) / ref;
    if (change > opt.change_ratio) return MotionState::Accelerating;
    if (change < -opt.change_ratio) return MotionState::Decelerating;
    return MotionState::Regular;
}

std::vector<MotionSegment> Runs(const std::vector<MotionPoint>& pts, const std::vector<double>& speed,
                                const std::vector<MotionState>& states) {
    std::vector<MotionSegment> out;
    const int n = static_cast<int>(states.size());
    int start = 0;
    for (int i = 1; i <= n; ++i) {
        if (i < n && states[static_cast<size_t>(i)] == states[static_cast<size_t>(start)]) continue;
        double sum = 0;
        for (int j = start; j < i; ++j) sum += speed[static_cast<size_t>(j)];
        MotionSegment seg;
        seg.state = states[static_cast<size_t>(start)];
        seg.start_ms = pts[static_cast<size_t>(start)].t_ms;
        seg.end_ms = pts[static_cast<size_t>(i - 1)].t_ms;
        seg.mean_speed = sum / static_cast<double>(i - start);
        seg.samples = i - start;
        out.push_back(seg);
        start = i;
    }
    return out;
}

// The first run shorter than min_ms merges into the neighbour whose mean
// speed is closer. A tie keeps the previous neighbour. The leading run has
// only the next one. The state is the side with more samples; a tie keeps
// the earlier side. Each merge shortens the list by one.
std::vector<MotionSegment> MergeShort(std::vector<MotionSegment> out, double min_ms) {
    if (out.size() < 2) return out;
    for (;;) {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(out.size()); ++i) {
            if (out[static_cast<size_t>(i)].Duration() < min_ms && out.size() > 1) {
                idx = i;
                break;
            }
        }
        if (idx < 0) return out;

        int target = idx - 1;
        if (idx == 0) {
            target = 1;
        } else if (idx + 1 < static_cast<int>(out.size())) {
            const double d_prev = std::abs(out[static_cast<size_t>(idx)].mean_speed -
                                           out[static_cast<size_t>(idx - 1)].mean_speed);
            const double d_next = std::abs(out[static_cast<size_t>(idx)].mean_speed -
                                           out[static_cast<size_t>(idx + 1)].mean_speed);
            if (d_next < d_prev) target = idx + 1;
        }

        int a = idx;
        int b = target;
        if (b < a) std::swap(a, b);
        const MotionSegment& left = out[static_cast<size_t>(a)];
        const MotionSegment& right = out[static_cast<size_t>(b)];
        MotionSegment merged;
        merged.state = right.state;
        merged.start_ms = left.start_ms;
        merged.end_ms = right.end_ms;
        merged.samples = left.samples + right.samples;
        if (left.samples >= right.samples) merged.state = left.state;
        merged.mean_speed = (left.mean_speed * static_cast<double>(left.samples) +
                             right.mean_speed * static_cast<double>(right.samples)) /
                            static_cast<double>(merged.samples);

        std::vector<MotionSegment> next;
        next.reserve(out.size() - 1);
        next.insert(next.end(), out.begin(), out.begin() + a);
        next.push_back(merged);
        next.insert(next.end(), out.begin() + (b + 1), out.end());
        out = std::move(next);
    }
}

}  // namespace

const char* MotionStateName(MotionState state) {
    switch (state) {
    case MotionState::Static: return "static";
    case MotionState::Starting: return "starting";
    case MotionState::Accelerating: return "accelerating";
    case MotionState::Regular: return "regular";
    case MotionState::Decelerating: return "decelerating";
    case MotionState::Stopping: return "stopping";
    }
    return "";
}

std::vector<MotionSegment> ClassifyMotion(const std::vector<MotionPoint>& points, ClassifyOptions opt) {
    ApplyDefaults(opt);
    if (points.size() < 2) return {};
    std::vector<double> speed = AbsSpeed(points);
    if (opt.smooth_radius > 0) speed = MovingAverage(speed, opt.smooth_radius);
    std::vector<MotionState> states(speed.size());
    for (int i = 0; i < static_cast<int>(speed.size()); ++i) states[static_cast<size_t>(i)] = ClassifyAt(speed, i, opt);
    return MergeShort(Runs(points, speed, states), opt.min_segment_ms);
}

}  // namespace samn
