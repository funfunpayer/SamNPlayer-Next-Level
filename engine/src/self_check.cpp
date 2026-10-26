#include "samn/engine/self_check.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace samn::engine {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kMadToSigma = 1.4826;

double Median(std::vector<double> v) {
    if (v.empty()) return kNaN;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
        m = (m + *std::max_element(v.begin(), v.begin() + static_cast<long>(mid))) / 2;
    }
    return m;
}

double Quantile(std::vector<double> v, double q) {
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (pos - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

std::vector<double> Finite(const std::vector<double>& v, int first, int end) {
    std::vector<double> out;
    for (int i = first; i < end; ++i)
        if (std::isfinite(v[static_cast<size_t>(i)])) out.push_back(v[static_cast<size_t>(i)]);
    return out;
}

double MeanOf(const std::vector<double>& v, int first, int end) {
    const auto f = Finite(v, first, end);
    if (f.empty()) return kNaN;
    double s = 0;
    for (double x : f) s += x;
    return s / static_cast<double>(f.size());
}

template <typename T>
std::vector<T> Slice(const std::vector<T>& v, int first, int end) {
    return std::vector<T>(v.begin() + first, v.begin() + end);
}

void AddReason(std::string* reasons, const char* name, double value, const char* op, double limit) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s%s %.2f %s %.2f", reasons->empty() ? "" : "; ", name, value, op, limit);
    *reasons += buf;
}

}  // namespace

std::vector<uint8_t> HampelMask(const std::vector<double>& y, int k, double factor, double eps) {
    std::vector<uint8_t> bad(y.size(), 0);
    const int n = static_cast<int>(y.size());
    std::vector<double> nb;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(y[static_cast<size_t>(i)])) continue;
        nb.clear();
        for (int j = std::max(0, i - k); j <= std::min(n - 1, i + k); ++j)
            if (j != i && std::isfinite(y[static_cast<size_t>(j)])) nb.push_back(y[static_cast<size_t>(j)]);
        if (nb.size() < 3) continue;
        const double med = Median(nb);
        std::vector<double> dev;
        dev.reserve(nb.size());
        for (double v : nb) dev.push_back(std::fabs(v - med));
        const double mad = Median(dev);
        if (std::fabs(y[static_cast<size_t>(i)] - med) > factor * (kMadToSigma * mad + eps)) bad[static_cast<size_t>(i)] = 1;
    }
    return bad;
}

SampleGateResult GateSamples(const std::vector<double>& times_ms, const std::vector<double>& positions,
                             const SampleGateOptions& o) {
    SampleGateResult r;
    const size_t n = std::min(times_ms.size(), positions.size());
    r.positions.assign(positions.begin(), positions.begin() + static_cast<long>(n));
    r.valid.assign(n, 0);
    for (size_t i = 0; i < n; ++i) r.valid[i] = std::isfinite(r.positions[i]) ? 1 : 0;

    // 1. Outliers: rejected, then treated like any other invalid sample.
    const auto bad = HampelMask(r.positions, o.hampel_k, o.hampel_factor, o.hampel_eps);
    for (size_t i = 0; i < n; ++i)
        if (bad[i]) {
            r.valid[i] = 0;
            r.positions[i] = kNaN;
            ++r.outliers;
        }

    // 2. Gaps: short invalid runs between two valid samples are interpolated
    // on the time axis; longer runs (or runs at the edges) stay invalid.
    size_t i = 0;
    while (i < n) {
        if (r.valid[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && !r.valid[j]) ++j;
        if (i > 0 && j < n && times_ms[j] - times_ms[i - 1] <= o.max_interp_gap_ms + 1e-6) {
            const double t0 = times_ms[i - 1], t1 = times_ms[j];
            const double y0 = r.positions[i - 1], y1 = r.positions[j];
            for (size_t k = i; k < j; ++k) {
                const double a = t1 > t0 ? (times_ms[k] - t0) / (t1 - t0) : 0.5;
                r.positions[k] = y0 + a * (y1 - y0);
                r.valid[k] = 1;
                ++r.interpolated;
            }
        }
        i = j;
    }

    // 3. Segments: continuous valid runs; too short ones are dropped.
    i = 0;
    while (i < n) {
        if (!r.valid[i]) {
            ++r.dropped;
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && r.valid[j]) ++j;
        if (static_cast<int>(j - i) >= o.min_segment_samples) {
            r.segments.emplace_back(static_cast<int>(i), static_cast<int>(j));
        } else {
            for (size_t k = i; k < j; ++k) {
                r.valid[k] = 0;
                r.positions[k] = kNaN;
                ++r.dropped;
            }
        }
        i = j;
    }
    return r;
}

double BoxOutsideShare(double x, double y, double w, double h, double width, double height) {
    if (!(w > 0 && h > 0)) return kNaN;
    const double ix = std::max(0.0, std::min(x + w, width) - std::max(x, 0.0));
    const double iy = std::max(0.0, std::min(y + h, height) - std::max(y, 0.0));
    return 1.0 - (ix * iy) / (w * h);
}

const char* WindowStateName(WindowState s) {
    switch (s) {
        case WindowState::kPass: return "pass";
        case WindowState::kSuspect: return "suspect";
        case WindowState::kFail: return "fail";
        case WindowState::kStill: return "still";
    }
    return "?";
}

double WindowCorrelation(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    double sa = 0, sb = 0;
    int m = 0;
    for (size_t i = 0; i < n; ++i)
        if (std::isfinite(a[i]) && std::isfinite(b[i])) {
            sa += a[i];
            sb += b[i];
            ++m;
        }
    if (m < 10) return kNaN;
    const double ma = sa / m, mb = sb / m;
    double cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < n; ++i)
        if (std::isfinite(a[i]) && std::isfinite(b[i])) {
            cov += (a[i] - ma) * (b[i] - mb);
            va += (a[i] - ma) * (a[i] - ma);
            vb += (b[i] - mb) * (b[i] - mb);
        }
    if (va < 1e-12 || vb < 1e-12) return kNaN;
    return cov / std::sqrt(va * vb);
}

std::vector<WindowVerdict> VerifyWindows(const std::vector<double>& y, const FrameChecks& c, const VerifyOptions& o) {
    std::vector<WindowVerdict> out;
    const int n = static_cast<int>(y.size());
    const int w = std::max(10, o.window_frames);
    auto has = [n](const auto& v) { return static_cast<int>(v.size()) == n; };

    // Velocity of the writer (central difference; NaN next to a lost sample).
    std::vector<double> vel(static_cast<size_t>(n), kNaN);
    for (int i = 1; i + 1 < n; ++i) vel[static_cast<size_t>(i)] = (y[static_cast<size_t>(i + 1)] - y[static_cast<size_t>(i - 1)]) / 2;

    // Windows: tiled per shot, never across a cut.
    std::vector<std::pair<int, int>> spans;
    {
        std::vector<int> starts{0};
        for (int cut : o.cuts)
            if (cut > 0 && cut < n && cut > starts.back()) starts.push_back(cut);
        starts.push_back(n);
        for (size_t s = 0; s + 1 < starts.size(); ++s) {
            const int shot_end = starts[s + 1];
            int a = s == 0 ? std::max(o.skip_frames, 0) : starts[s] + o.cut_guard_frames;
            while (a + w <= shot_end) {
                int b = a + w;
                if (shot_end - b < w / 2) b = shot_end;  // join a short tail
                spans.emplace_back(a, b);
                a = b;
            }
        }
    }

    // References over the windows: foreground level and the typical range.
    std::vector<double> fg_means, ranges;
    for (const auto& [a, b] : spans) {
        if (has(c.foreground)) fg_means.push_back(MeanOf(c.foreground, a, b));
        const auto f = Finite(y, a, b);
        if (f.size() >= 10) ranges.push_back(Quantile(f, 0.98) - Quantile(f, 0.02));
    }
    double fg_ref = o.foreground_ref;
    if (fg_ref <= 0 && !fg_means.empty()) {
        std::vector<double> first(fg_means.begin(), fg_means.begin() + std::min<long>(10, static_cast<long>(fg_means.size())));
        first.erase(std::remove_if(first.begin(), first.end(), [](double v) { return !std::isfinite(v); }), first.end());
        fg_ref = Median(first);
    }
    const double range_ref = Quantile(ranges, 0.75);

    for (const auto& [a, b] : spans) {
        const int len = b - a;
        WindowVerdict v;
        v.first = a;
        v.end = b;

        // c4 tracker ok-rate (trigger)
        if (has(c.tracker_ok)) {
            int ok = 0;
            for (int i = a; i < b; ++i) ok += c.tracker_ok[static_cast<size_t>(i)] ? 1 : 0;
            v.c4 = static_cast<double>(ok) / len;
        } else {
            v.c4 = kNaN;
        }
        // c3 Hampel rate (trigger), over the samples that exist
        {
            const auto ys = Slice(y, a, b);
            const auto bad = HampelMask(ys, o.hampel.hampel_k, o.hampel.hampel_factor, o.hampel.hampel_eps);
            int finite = 0, out_n = 0;
            for (int i = 0; i < len; ++i) {
                finite += std::isfinite(ys[static_cast<size_t>(i)]) ? 1 : 0;
                out_n += bad[static_cast<size_t>(i)];
            }
            v.c3 = finite >= 10 ? static_cast<double>(out_n) / finite : kNaN;
        }
        // c8 range relative to the typical window (trigger)
        {
            const auto f = Finite(y, a, b);
            v.c8 = (f.size() >= 10 && range_ref > 1e-9) ? (Quantile(f, 0.98) - Quantile(f, 0.02)) / range_ref : kNaN;
        }
        // c6 camera trust (context for c1)
        v.c6 = has(c.camera_inliers) ? MeanOf(c.camera_inliers, a, b) : kNaN;
        // c1 flow agreement (independent), unknown when the camera estimate is not trusted
        v.c1 = kNaN;
        if (has(c.box_flow) && !(std::isfinite(v.c6) && v.c6 < o.c6_min))
            v.c1 = WindowCorrelation(Slice(vel, a, b), Slice(c.box_flow, a, b));
        // c2 foreground presence (independent)
        v.c2 = (has(c.foreground) && fg_ref > 1e-9) ? MeanOf(c.foreground, a, b) / fg_ref : kNaN;
        // c7 appearance against the original seed (independent)
        v.c7 = has(c.appearance) ? MeanOf(c.appearance, a, b) : kNaN;
        // c9 box outside the picture (geometry, not the tracker's opinion)
        v.c9 = has(c.box_outside) ? MeanOf(c.box_outside, a, b) : kNaN;

        // A writer that cannot be computed is not "unknown": it is lost.
        const bool lost = std::isfinite(v.c4) ? v.c4 < o.c4_min : Finite(y, a, b).size() < static_cast<size_t>(len) * 7 / 10;

        // Still: the writer's range collapsed, the box sees no motion, the
        // tracker holds the box, and something independent confirms it is a
        // pause and not a lost target (a quiet scene, or the seed's look).
        bool still = false;
        if (!lost && std::isfinite(v.c8) && v.c8 < o.c8_min) {
            bool box_quiet = true;
            if (has(c.box_flow)) {
                std::vector<double> mag;
                for (int i = a; i < b; ++i)
                    if (std::isfinite(c.box_flow[static_cast<size_t>(i)])) mag.push_back(std::fabs(c.box_flow[static_cast<size_t>(i)]));
                box_quiet = !mag.empty() && Median(mag) < o.still_flow;
            }
            const double scene = has(c.scene_motion) ? MeanOf(c.scene_motion, a, b) : kNaN;
            const bool confirmed = (std::isfinite(scene) && scene < o.still_scene) ||
                                   (std::isfinite(v.c7) && v.c7 >= o.c7_min);
            still = box_quiet && confirmed;
        }

        int independent_fail = 0, trigger_fail = 0;
        std::string r;
        if (std::isfinite(v.c1) && v.c1 < o.c1_min) { ++independent_fail; AddReason(&r, "c1 flow", v.c1, "<", o.c1_min); }
        if (std::isfinite(v.c2) && v.c2 < o.c2_min) { ++independent_fail; AddReason(&r, "c2 foreground", v.c2, "<", o.c2_min); }
        if (std::isfinite(v.c7) && v.c7 < o.c7_min) { ++independent_fail; AddReason(&r, "c7 appearance", v.c7, "<", o.c7_min); }
        if (std::isfinite(v.c9) && v.c9 > o.c9_max) { ++independent_fail; AddReason(&r, "c9 outside", v.c9, ">", o.c9_max); }
        if (lost) { ++trigger_fail; AddReason(&r, "c4 ok", std::isfinite(v.c4) ? v.c4 : 0.0, "<", o.c4_min); }
        if (std::isfinite(v.c3) && v.c3 > o.c3_max) { ++trigger_fail; AddReason(&r, "c3 hampel", v.c3, ">", o.c3_max); }
        if (std::isfinite(v.c8) && v.c8 < o.c8_min) { ++trigger_fail; AddReason(&r, "c8 range", v.c8, "<", o.c8_min); }

        if (still) {
            v.state = WindowState::kStill;
        } else if (independent_fail > 0 || trigger_fail >= 2 || lost) {
            v.state = WindowState::kFail;
        } else if (trigger_fail == 1) {
            v.state = WindowState::kSuspect;
        } else {
            v.state = WindowState::kPass;
        }
        v.reasons = std::move(r);
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace samn::engine
