#include "samn/engine/post_track.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>

#include "samn/simplify.h"
#include "track_parts.h"

namespace samn::engine {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::pair<double, double> MinMax(const std::vector<double>& v) {
    if (v.empty()) return {0, 0};
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return {*lo, *hi};
}

double Ptp(const std::vector<double>& v) {
    const auto [lo, hi] = MinMax(v);
    return hi - lo;
}

double MedianDiff(const std::vector<double>& t) {
    if (t.size() < 2) return 1;
    std::vector<double> d;
    for (size_t i = 1; i < t.size(); ++i) d.push_back(t[i] - t[i - 1]);
    std::sort(d.begin(), d.end());
    const size_t mid = d.size() / 2;
    return d.size() % 2 ? d[mid] : (d[mid - 1] + d[mid]) / 2;
}

double Clip01(double v) { return std::clamp(v, 0.0, 1.0); }

std::vector<double> PadEdge(const std::vector<double>& v, int pad) {
    std::vector<double> out(v.size() + 2 * pad);
    std::fill(out.begin(), out.begin() + pad, v.front());
    std::copy(v.begin(), v.end(), out.begin() + pad);
    std::fill(out.end() - pad, out.end(), v.back());
    return out;
}

// np.convolve(np.pad(x, k, "edge"), np.ones(k)/k, "same")[k:-k]
std::vector<double> MovingAverageEdge(const std::vector<double>& v, int k) {
    const int n = static_cast<int>(v.size());
    if (k < 1 || n == 0) return v;
    const std::vector<double> padded = PadEdge(v, k);
    std::vector<double> full(padded.size() + k - 1, 0.0);
    const double inv = 1.0 / k;
    for (size_t i = 0; i < padded.size(); ++i)
        for (int j = 0; j < k; ++j) full[i + j] += padded[i] * inv;
    const int start = (k - 1) / 2;
    return std::vector<double>(full.begin() + start + k, full.begin() + start + k + n);
}

// Lifts weak sections toward the usable amplitude (dynamic_range_normalize).
std::vector<double> DynamicRangeNormalize(const std::vector<double>& v, int window, double max_gain,
                                          double min_local_span) {
    const int n = static_cast<int>(v.size());
    if (window < 8 || n < window * 2) return v;
    const double global_span = Ptp(v);
    if (global_span < 1e-9) return v;
    const int half = window / 2;
    const std::vector<double> padded = PadEdge(v, half);
    std::vector<double> centers(n), spans(n);
    for (int i = 0; i < n; ++i) {
        const std::vector<double> seg(padded.begin() + i, padded.begin() + i + window);
        const double lo = Percentile(seg, 5), hi = Percentile(seg, 95);
        centers[i] = (lo + hi) / 2.0;
        spans[i] = hi - lo;
    }
    const int smooth = std::max(3, window / 2);
    centers = MovingAverageEdge(centers, smooth);
    spans = MovingAverageEdge(spans, smooth);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        double gain = 1.0;
        if (spans[i] > global_span * min_local_span)
            gain = std::clamp(global_span / std::max(spans[i], 1e-9), 1.0, max_gain);
        out[i] = centers[i] + (v[i] - centers[i]) * gain;
    }
    return out;
}

std::vector<double> OnePole(const std::vector<double>& x, double alpha, bool highpass) {
    std::vector<double> y(x.size());
    if (x.empty()) return y;
    y[0] = x[0];
    for (size_t i = 1; i < x.size(); ++i)
        y[i] = highpass ? alpha * (y[i - 1] + x[i] - x[i - 1]) : y[i - 1] + alpha * (x[i] - y[i - 1]);
    return y;
}

// One-pole filter forward, then backward (zero phase).
std::vector<double> FiltFilt(const std::vector<double>& x, double alpha, bool highpass) {
    if (alpha <= 0 || alpha >= 1 || x.empty()) return x;
    std::vector<double> y = OnePole(x, alpha, highpass);
    std::reverse(y.begin(), y.end());
    y = OnePole(y, alpha, highpass);
    std::reverse(y.begin(), y.end());
    return y;
}

double PeakProminence(const std::vector<double>& y, int peak) {
    const int n = static_cast<int>(y.size());
    if (peak <= 0 || peak >= n - 1) return 0;
    double left = y[peak], right = y[peak];
    for (int i = peak; i >= 0; --i) {
        left = std::min(left, y[i]);
        if (y[i] > y[peak]) break;
    }
    for (int i = peak; i < n; ++i) {
        right = std::min(right, y[i]);
        if (y[i] > y[peak]) break;
    }
    return y[peak] - std::max(left, right);  // the higher of the two bases
}

std::vector<int> LocalMaxima(const std::vector<double>& y) {
    const int n = static_cast<int>(y.size());
    std::vector<int> out;
    int i = 1;
    while (i < n - 1) {
        if (y[i] < y[i - 1]) {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 < n && y[j + 1] == y[i]) ++j;
        if (y[i] > y[i - 1] && j + 1 < n && y[j] > y[j + 1]) out.push_back((i + j) / 2);
        i = j + 1;
    }
    return out;
}

std::vector<int> UniqueSorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// Adds points where a straight line between extrema would miss the curve
// (refine_keyframes), worst first, at most max_extra_ratio x the base count.
std::vector<int> RefineKeyframes(const std::vector<double>& t, const std::vector<double>& pos,
                                 const std::vector<int>& base, double max_error, double max_extra_ratio) {
    if (max_error <= 0 || base.size() < 2) return base;
    const size_t limit = static_cast<size_t>(base.size() * max_extra_ratio);
    std::vector<int> result = UniqueSorted(base);
    struct Item {
        double dev;
        int start, end, split;
        bool operator<(const Item& o) const { return dev < o.dev; }
    };
    std::priority_queue<Item> heap;
    auto worst = [&](int a, int b) {
        if (b - a < 2) return;
        const double span = t[b] - t[a];
        if (span <= 0) return;
        double max_dev = -1;
        int max_idx = -1;
        for (int i = a + 1; i < b; ++i) {
            const double d = std::abs(pos[i] - (pos[a] + (pos[b] - pos[a]) * (t[i] - t[a]) / span));
            if (d > max_dev) {
                max_dev = d;
                max_idx = i;
            }
        }
        if (max_idx >= 0 && max_dev > max_error) heap.push({max_dev, a, b, max_idx});
    };
    for (size_t i = 0; i + 1 < result.size(); ++i) worst(result[i], result[i + 1]);
    std::set<int> added;
    while (!heap.empty() && result.size() + added.size() < limit) {
        const Item it = heap.top();
        heap.pop();
        added.insert(it.split);
        worst(it.start, it.split);
        worst(it.split, it.end);
    }
    result.insert(result.end(), added.begin(), added.end());
    return UniqueSorted(result);
}

// Drops the less important of two keyframes closer than min_interval_ms
// (enforce_min_interval); the ends stay.
std::vector<int> EnforceMinInterval(const std::vector<double>& t, const std::vector<double>& pos, std::vector<int> r,
                                    double min_interval_ms) {
    if (min_interval_ms <= 0 || r.size() < 3) return r;
    auto importance = [&](int k) {
        if (k <= 0 || k >= static_cast<int>(r.size()) - 1) return 1e300;
        const int l = r[k - 1], m = r[k], rt = r[k + 1];
        const double span = t[rt] - t[l];
        if (span <= 0) return 0.0;
        return std::abs(pos[m] - (pos[l] + (pos[rt] - pos[l]) * (t[m] - t[l]) / span));
    };
    bool changed = true;
    while (changed && r.size() > 2) {
        changed = false;
        const int last = static_cast<int>(r.size()) - 1;
        for (int i = 0; i < last; ++i) {
            if (t[r[i + 1]] - t[r[i]] >= min_interval_ms) continue;
            int drop = importance(i) > importance(i + 1) ? i + 1 : i;
            if (drop == 0 || drop == last) drop = i == 0 ? i + 1 : i;
            if (drop == 0 || drop == last) continue;
            r.erase(r.begin() + drop);
            changed = true;
            break;
        }
    }
    return r;
}

}  // namespace

double Percentile(std::vector<double> v, double p) {
    const size_t n = v.size();
    if (n == 0) return 0;
    if (n == 1) return v[0];
    std::sort(v.begin(), v.end());
    if (p <= 0) return v.front();
    if (p >= 100) return v.back();
    const double rank = p / 100.0 * (n - 1);
    const size_t lo = static_cast<size_t>(std::floor(rank)), hi = static_cast<size_t>(std::ceil(rank));
    if (lo == hi) return v[lo];
    const double w = rank - lo;
    return v[lo] * (1 - w) + v[hi] * w;
}

std::vector<double> RollingDetrend(const std::vector<double>& values, double window_ms, double step_ms) {
    const int n = static_cast<int>(values.size());
    if (n == 0 || window_ms <= 0 || step_ms <= 0) return values;
    int win = std::max(3, static_cast<int>(std::round(window_ms / step_ms)));
    if (win % 2 == 0) ++win;
    if (win > n) {
        win = n % 2 ? n : n - 1;
        if (win < 3) return values;
    }
    const int half = win / 2;
    std::vector<double> pref(n + 1, 0.0), out(n);
    for (int i = 0; i < n; ++i) pref[i + 1] = pref[i] + values[i];
    for (int i = 0; i < n; ++i) {
        const int lo = std::max(0, i - half), hi = std::min(n - 1, i + half);
        out[i] = values[i] - (pref[hi + 1] - pref[lo]) / (hi - lo + 1);
    }
    return out;
}

std::vector<double> Bandpass(const std::vector<double>& values, double sample_hz, double low_hz, double high_hz) {
    if (values.size() < 8 || sample_hz <= 0 || (low_hz <= 0 && high_hz <= 0)) return values;
    if (high_hz > 0 && low_hz > high_hz) std::swap(low_hz, high_hz);
    const double dt = 1.0 / sample_hz;
    std::vector<double> out = values;
    if (low_hz > 0 && low_hz < sample_hz / 2) {
        const double rc = 1.0 / (2 * kPi * low_hz);
        out = FiltFilt(out, rc / (rc + dt), true);
    }
    if (high_hz > 0 && high_hz < sample_hz / 2) {
        const double rc = 1.0 / (2 * kPi * high_hz);
        out = FiltFilt(out, dt / (rc + dt), false);
    }
    return out;
}

std::vector<int> FindPeaks(const std::vector<double>& y, int distance, double prominence) {
    const int n = static_cast<int>(y.size());
    if (n < 3) return {};
    distance = std::max(1, distance);
    std::vector<int> cands = LocalMaxima(y);
    if (prominence > 0)
        cands.erase(std::remove_if(cands.begin(), cands.end(),
                                   [&](int i) { return PeakProminence(y, i) < prominence; }),
                    cands.end());
    if (distance <= 1 || cands.empty()) return cands;
    // Taller peaks first, then keep each one far enough from those kept (scipy).
    std::sort(cands.begin(), cands.end(), [&](int a, int b) { return y[a] != y[b] ? y[a] > y[b] : a < b; });
    std::vector<char> kept(n, 0);
    std::vector<int> out;
    for (int idx : cands) {
        bool ok = true;
        for (int j = std::max(0, idx - distance + 1); j < std::min(n, idx + distance) && ok; ++j) ok = !kept[j];
        if (ok) {
            kept[idx] = 1;
            out.push_back(idx);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

namespace {

// Frames that are missing (dropped, variable frame rate, an analysis-rate
// cap) leave uneven steps; the smoothing and the peak distance count samples,
// so uneven steps move the turning points (spec T4: up to 66 ms). Such input
// is resampled onto the median step by linear interpolation; even input is
// returned as it is, so every regular track stays byte-identical.
bool ResampleIfUneven(std::vector<double>& t, std::vector<double>& y) {
    const size_t n = t.size();
    if (n < 4) return false;
    const double step = MedianDiff(t);
    if (step <= 0) return false;
    bool uneven = false;
    for (size_t i = 1; i < n && !uneven; ++i) uneven = t[i] - t[i - 1] > 1.5 * step;
    if (!uneven) return false;
    std::vector<double> rt, ry;
    const size_t m = static_cast<size_t>(std::floor((t.back() - t.front()) / step)) + 1;
    rt.reserve(m);
    ry.reserve(m);
    size_t j = 0;
    for (size_t k = 0; k < m; ++k) {
        const double at = t.front() + double(k) * step;
        while (j + 1 < n && t[j + 1] < at) ++j;
        const size_t a = j, b = std::min(j + 1, n - 1);
        const double span = t[b] - t[a];
        const double f = span > 0 ? std::clamp((at - t[a]) / span, 0.0, 1.0) : 0.0;
        rt.push_back(std::round(at));
        ry.push_back(y[a] + (y[b] - y[a]) * f);
    }
    t = std::move(rt);
    y = std::move(ry);
    return true;
}

}  // namespace

std::optional<PostResult> PositionsToActions(const std::vector<double>& t_in, const std::vector<double>& raw_in,
                                             const PostOptions& o, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return std::optional<PostResult>();
    };
    if (raw_in.empty() || t_in.size() != raw_in.size()) return fail("timestamps and positions differ in length");
    std::vector<double> t = t_in, raw = raw_in;
    // Scene ranges are sample indices of the input: those runs keep its steps.
    if (o.scene_ranges.empty()) ResampleIfUneven(t, raw);
    const int n = static_cast<int>(raw.size());

    int window = o.smooth_window > 0 ? o.smooth_window : 11;
    if (n < window) window = n % 2 == 0 ? n - 1 : n;
    std::vector<double> s = raw;
    if (window >= 5) s = Savgol(raw, window % 2 == 0 ? window + 1 : window, 3);

    const double step_ms = MedianDiff(t);
    const double sample_hz = 1000.0 / std::max(step_ms, 1.0);
    if (o.detrend_window_ms > 0 && n > 4) s = RollingDetrend(s, o.detrend_window_ms, step_ms);
    if ((o.bandpass_low_hz > 0 || o.bandpass_high_hz > 0) && n > 8)
        s = Bandpass(s, sample_hz, o.bandpass_low_hz, o.bandpass_high_hz);
    if (o.dynamic_range_ms > 0 && n > 4)
        s = DynamicRangeNormalize(s, static_cast<int>(o.dynamic_range_ms / std::max(step_ms, 1.0)), 5.0, 0.12);

    std::vector<double> pos(n);
    auto range_of = [&](const std::vector<double>& seg) {
        return o.norm_percentile > 0
                   ? std::make_pair(Percentile(seg, o.norm_percentile), Percentile(seg, 100.0 - o.norm_percentile))
                   : MinMax(seg);
    };
    if (!o.scene_ranges.empty()) {
        pos = s;
        for (auto [start, end] : o.scene_ranges) {
            end = std::min(end, n);
            if (end - start < 3) continue;
            const std::vector<double> seg(s.begin() + start, s.begin() + end);
            auto [lo, hi] = range_of(seg);
            if (hi - lo < 1e-6) std::tie(lo, hi) = MinMax(seg);
            for (int i = start; i < end; ++i) pos[i] = hi - lo < 1e-6 ? 0.5 : Clip01((s[i] - lo) / (hi - lo));
        }
        for (double& p : pos) p *= 100.0;
    } else {
        auto [lo, hi] = range_of(s);
        if (hi - lo < 1e-6) std::tie(lo, hi) = MinMax(s);
        if (hi - lo < 1e-6)
            return fail(
                "no discernible motion in the tracked region: check the box size and place (avoid near-full-frame "
                "boxes); the filters need visible stroke motion");
        for (int i = 0; i < n; ++i) pos[i] = Clip01((s[i] - lo) / (hi - lo)) * 100.0;
    }
    if (!o.invert)
        for (double& p : pos) p = 100.0 - p;

    const double fps = n > 1 ? 1000.0 / MedianDiff(t) : 30.0;
    const int min_dist = std::max(1, static_cast<int>(o.min_peak_distance_ms / 1000.0 * fps));
    double prominence = 0;
    if (o.peak_prominence > 0 && Ptp(pos) > 1e-9) prominence = Ptp(pos) * o.peak_prominence;
    std::vector<double> neg(n);
    std::transform(pos.begin(), pos.end(), neg.begin(), [](double v) { return -v; });
    std::vector<int> keys{0, n - 1};
    for (int i : FindPeaks(pos, min_dist, prominence)) keys.push_back(i);
    for (int i : FindPeaks(neg, min_dist, prominence)) keys.push_back(i);
    keys = UniqueSorted(keys);

    if (o.adaptive_error > 0) keys = RefineKeyframes(t, pos, keys, o.adaptive_error, 2.0);
    if (o.min_interval_ms > 0) keys = EnforceMinInterval(t, pos, keys, o.min_interval_ms);
    if (o.rdp_tolerance > 0 && keys.size() > 2) {
        std::vector<SimplifyPoint> pts;
        for (int k : keys) pts.push_back({t[k], pos[k]});
        std::vector<int> kept;
        for (int li : SimplifyIndices(pts, o.rdp_tolerance)) kept.push_back(keys[li]);
        keys = kept;
    }

    PostResult r;
    for (int k : keys) r.actions.push_back({static_cast<int64_t>(t[k]), static_cast<int>(std::round(pos[k]))});
    r.dense_at = t;
    r.dense_pos = pos;
    return r;
}

std::vector<uint8_t> FindOutliers(const std::vector<double>& y, const std::vector<uint8_t>& bad, int neighbours,
                                  double factor) {
    const int n = static_cast<int>(y.size());
    std::vector<uint8_t> out(n, 0);
    if (factor <= 0 || neighbours < 1) return out;
    std::vector<double> win, dev;
    win.reserve(2 * neighbours);
    dev.reserve(2 * neighbours);
    for (int i = 0; i < n; ++i) {
        if (!bad.empty() && bad[i]) continue;
        win.clear();
        for (int j = std::max(0, i - neighbours); j <= std::min(n - 1, i + neighbours); ++j)
            if (j != i && (bad.empty() || !bad[j])) win.push_back(y[j]);
        if (win.size() < 2) continue;
        const double med = Percentile(win, 50);
        dev.clear();
        for (double v : win) dev.push_back(std::abs(v - med));
        const double mad = std::max(Percentile(dev, 50), kOutlierMadFloor);
        if (std::abs(y[i] - med) > factor * mad) out[i] = 1;
    }
    return out;
}

GapPlan PlanGaps(const std::vector<double>& t, const std::vector<double>& y, std::vector<uint8_t> bad,
                 const GapOptions& o) {
    const int n = static_cast<int>(y.size());
    GapPlan plan;
    plan.values = y;
    if (n == 0 || t.size() != y.size()) return plan;
    if (bad.size() != y.size()) bad.assign(n, 0);
    if (o.outlier_mad_factor > 0) {
        const std::vector<uint8_t> out = FindOutliers(y, bad, o.outlier_neighbours, o.outlier_mad_factor);
        for (int i = 0; i < n; ++i)
            if (out[i]) {
                bad[i] = 1;
                ++plan.outliers;
            }
    }
    const auto too_long = [&](double span_ms) { return o.max_gap_ms > 0 && span_ms > o.max_gap_ms; };
    int seg_start = -1, prev_good = -1;
    for (int i = 0; i <= n; ++i) {
        if (i < n && bad[i]) continue;
        // i is good (or the end); bad samples lie between prev_good and i.
        const bool gap = i - prev_good > 1;
        if (prev_good < 0) {
            if (i == n) break;  // nothing usable
            if (gap && !too_long(t[i] - t[0])) {
                for (int k = 0; k < i; ++k) plan.values[k] = y[i];  // short lead-in: hold
                seg_start = 0;
                ++plan.bridged;
            } else {
                if (gap) ++plan.split;
                seg_start = i;
            }
        } else if (i == n) {
            if (gap && !too_long(t[n - 1] - t[prev_good])) {
                for (int k = prev_good + 1; k < n; ++k) plan.values[k] = y[prev_good];  // short tail: hold
                plan.segments.push_back({seg_start, n});
                ++plan.bridged;
            } else {
                if (gap) ++plan.split;
                plan.segments.push_back({seg_start, prev_good + 1});
            }
            break;
        } else if (gap) {
            const double span = t[i] - t[prev_good];
            if (too_long(span)) {
                plan.segments.push_back({seg_start, prev_good + 1});
                seg_start = i;
                ++plan.split;
            } else {
                for (int k = prev_good + 1; k < i; ++k) {
                    const double w = span > 0 ? (t[k] - t[prev_good]) / span : 0;
                    plan.values[k] = y[prev_good] + (y[i] - y[prev_good]) * w;
                }
                ++plan.bridged;
            }
        }
        prev_good = i;
    }
    return plan;
}

std::vector<Action> LimitSpeed(const std::vector<Action>& actions, double max_ups, int* changed) {
    if (changed) *changed = 0;
    if (max_ups <= 0 || actions.size() < 2) return actions;
    std::vector<Action> out{actions.front()};
    for (size_t i = 1; i < actions.size(); ++i) {
        const Action& a = actions[i];
        const int64_t dt = a.at - out.back().at;
        if (dt <= 0) {
            out.push_back(a);
            continue;
        }
        const double max_delta = max_ups * dt / 1000.0;
        const double delta = a.pos - out.back().pos;
        if (std::abs(delta) > max_delta) {
            const double p = std::clamp(out.back().pos + (delta < 0 ? -max_delta : max_delta), 0.0, 100.0);
            out.push_back({a.at, static_cast<int>(std::round(p))});
            if (changed) ++*changed;
        } else {
            out.push_back(a);
        }
    }
    return out;
}

std::vector<Action> ClampActionsPos(const std::vector<Action>& actions, int lo, int hi) {
    std::vector<Action> out = actions;
    for (Action& a : out) a.pos = std::clamp(a.pos, lo, hi);
    return out;
}

}  // namespace samn::engine
