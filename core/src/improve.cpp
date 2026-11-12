#include "samn/improve.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace samn {

namespace {

int ClampPos(long p) { return int(std::clamp<long>(p, 0, 100)); }

void SortByTime(std::vector<Action>& a) {
    std::stable_sort(a.begin(), a.end(), [](const Action& x, const Action& y) { return x.at < y.at; });
}

int InterpAt(const std::vector<Action>& a, int64_t t) {
    if (a.empty()) return 50;
    if (t <= a.front().at) return a.front().pos;
    if (t >= a.back().at) return a.back().pos;
    for (size_t i = 1; i < a.size(); ++i) {
        const Action& p = a[i - 1];
        const Action& n = a[i];
        if (t >= p.at && t <= n.at) {
            if (n.at == p.at) return n.pos;
            const double f = double(t - p.at) / double(n.at - p.at);
            return int(std::lround(p.pos + f * (n.pos - p.pos)));
        }
    }
    return a.back().pos;
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

double MedianSpacingMs(const std::vector<Action>& a) {
    std::vector<double> dts;
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i].at > a[i - 1].at) dts.push_back(double(a[i].at - a[i - 1].at));
    return dts.empty() ? 0 : Median(std::move(dts));
}

int64_t StepOrDefault(int64_t step_ms, double audio_hz) {
    if (step_ms > 0) return step_ms;
    int64_t step = kDefaultFillStepMs;
    if (audio_hz > 0.2 && audio_hz < 5) {
        // Half a period, so a full stroke (up and down) lands near the tempo.
        const int64_t half = std::llround(1000.0 / (2.0 * audio_hz));
        if (half >= 40 && half <= 800) step = half;
    }
    return step;
}

std::vector<TrackingGap> MergeGaps(const std::vector<TrackingGap>& gaps) {
    std::vector<TrackingGap> clean;
    for (TrackingGap g : gaps) {
        if (g.end_ms < g.start_ms) std::swap(g.start_ms, g.end_ms);
        if (g.end_ms > g.start_ms) clean.push_back(g);
    }
    std::sort(clean.begin(), clean.end(), [](const TrackingGap& a, const TrackingGap& b) {
        return a.start_ms == b.start_ms ? a.end_ms < b.end_ms : a.start_ms < b.start_ms;
    });
    std::vector<TrackingGap> out;
    for (const auto& g : clean) {
        if (!out.empty() && g.start_ms <= out.back().end_ms) {
            out.back().end_ms = std::max(out.back().end_ms, g.end_ms);
            continue;
        }
        out.push_back(g);
    }
    return out;
}

void PushOrReplace(std::vector<Action>& out, const Action& a) {
    if (!out.empty() && out.back().at == a.at) out.back() = a;
    else out.push_back(a);
}

}  // namespace

int64_t AutoFillGapThreshold(const std::vector<Action>& sorted) {
    const double med = MedianSpacingMs(sorted);
    if (med <= 0) return kDefaultFillGapMs;
    const auto doctor = int64_t(std::max(med * 8, 2000.0));
    return std::min(doctor, kDefaultFillGapMs);
}

std::vector<Action> TrimActions(std::vector<Action> a, int64_t start_ms, int64_t end_ms) {
    if (a.size() < 2) throw std::invalid_argument("funscript: trim needs at least 2 points");
    SortByTime(a);
    const int64_t lo = a.front().at, hi = a.back().at;
    if (start_ms < 0) start_ms = lo;
    if (end_ms < 0) end_ms = hi;
    if (end_ms < start_ms) std::swap(start_ms, end_ms);
    if (start_ms <= lo && end_ms >= hi) return a;
    std::vector<Action> out;
    out.reserve(a.size() + 2);
    if (start_ms > lo) out.push_back({start_ms, ClampPos(InterpAt(a, start_ms))});
    for (const auto& x : a)
        if (x.at >= start_ms && x.at <= end_ms) PushOrReplace(out, x);
    if (end_ms < hi) PushOrReplace(out, {end_ms, ClampPos(InterpAt(a, end_ms))});
    if (out.size() < 2) throw std::invalid_argument("funscript: trim would leave fewer than 2 points");
    return out;
}

std::vector<Action> FillGaps(std::vector<Action> a, int64_t max_gap_ms, int64_t step_ms, double audio_hz,
                             int* gaps_filled, int* points_added) {
    int gaps = 0, added = 0;
    if (a.size() >= 2) {
        SortByTime(a);
        if (max_gap_ms <= 0) max_gap_ms = AutoFillGapThreshold(a);
        int64_t step = std::max<int64_t>(20, StepOrDefault(step_ms, audio_hz));
        std::vector<Action> out;
        out.reserve(a.size() * 2);
        out.push_back(a.front());
        for (size_t i = 1; i < a.size(); ++i) {
            const Action prev = out.back();
            const Action& next = a[i];
            const int64_t dt = next.at - prev.at;
            if (dt > max_gap_ms && dt > step) {
                ++gaps;
                const int64_t n = dt / step;
                for (int64_t k = 1; k < n; ++k) {
                    const int64_t t = prev.at + k * step;
                    if (t >= next.at) break;
                    const double f = double(t - prev.at) / double(dt);
                    out.push_back({t, ClampPos(std::lround(prev.pos + f * (next.pos - prev.pos)))});
                    ++added;
                }
            }
            PushOrReplace(out, next);
        }
        a = std::move(out);
    }
    if (gaps_filled) *gaps_filled = gaps;
    if (points_added) *points_added = added;
    return a;
}

namespace {

constexpr int64_t kStrokeContextMs = 8000;
constexpr double kMinHalfPeriodMs = 30000.0 / kBaseBpmMax;  // 214 ms
constexpr double kMaxHalfPeriodMs = 30000.0 / kBaseBpmMin;  // 750 ms

struct StrokeStats {
    std::vector<double> halves;  // ms between turning points
    std::vector<double> amps;    // position change between them
};

// Turning points of a[from, to] (inclusive), added to stats.
void AddTurns(const std::vector<Action>& a, size_t from, size_t to, StrokeStats& st) {
    if (to <= from) return;
    std::vector<size_t> turns{from};
    int dir = 0;
    for (size_t i = from + 1; i <= to; ++i) {
        const int d = (a[i].pos > a[i - 1].pos) - (a[i].pos < a[i - 1].pos);
        if (d == 0) continue;
        if (dir != 0 && d != dir) turns.push_back(i - 1);
        dir = d;
    }
    turns.push_back(to);
    for (size_t k = 1; k < turns.size(); ++k) {
        const Action& p = a[turns[k - 1]];
        const Action& n = a[turns[k]];
        if (n.at <= p.at) continue;
        st.halves.push_back(double(n.at - p.at));
        st.amps.push_back(std::abs(double(n.pos - p.pos)));
    }
}

// Direction of the last movement up to a[i]: +1 up, -1 down, 0 none.
int LastDirection(const std::vector<Action>& a, size_t i) {
    for (size_t k = i; k > 0; --k) {
        if (a[k].pos != a[k - 1].pos) return a[k].pos > a[k - 1].pos ? 1 : -1;
    }
    return 0;
}

}  // namespace

std::vector<Action> FillGapsRhythmic(std::vector<Action> a, int64_t max_gap_ms, int64_t step_ms, double audio_hz,
                                     const BaseRhythm& base, int* gaps_filled, int* points_added) {
    int gaps = 0, added = 0;
    if (a.size() >= 2) {
        SortByTime(a);
        if (max_gap_ms <= 0) max_gap_ms = AutoFillGapThreshold(a);
        const int64_t line_step = std::max<int64_t>(20, StepOrDefault(step_ms, audio_hz));
        // The whole script's stroke amplitude, for gaps without strokes near them.
        StrokeStats all;
        AddTurns(a, 0, a.size() - 1, all);
        const double global_amp = all.amps.empty() ? 0 : Median(all.amps);
        double audio_bpm = 0;
        if (audio_hz > 0 && std::isfinite(audio_hz)) {
            audio_bpm = audio_hz * 60;
            while (audio_bpm > kBaseBpmMax) audio_bpm /= 2;
            while (audio_bpm < kBaseBpmMin) audio_bpm *= 2;
        }
        std::vector<Action> out;
        out.reserve(a.size() * 2);
        out.push_back(a.front());
        for (size_t i = 1; i < a.size(); ++i) {
            const Action prev = out.back();
            const Action& next = a[i];
            const int64_t dt = next.at - prev.at;
            // FillGaps' own rule for which gaps are filled at all.
            if (dt <= max_gap_ms || dt <= line_step) {
                PushOrReplace(out, next);
                continue;
            }
            // Too short for a stroke: a straight line, as FillGaps.
            const bool too_short = dt <= 2 * int64_t(kMinHalfPeriodMs);
            // Strokes near the gap, each side on its own (never across it).
            StrokeStats near;
            size_t lo = i - 1;
            while (lo > 0 && a[lo - 1].at >= prev.at - kStrokeContextMs) --lo;
            AddTurns(a, lo, i - 1, near);
            size_t hi = i;
            while (hi + 1 < a.size() && a[hi + 1].at <= next.at + kStrokeContextMs) ++hi;
            AddTurns(a, i, hi, near);
            double half = 0, amp = global_amp;
            bool holding = too_short;
            if (!too_short && near.halves.size() >= 3) {
                const double h = Median(near.halves);
                amp = Median(near.amps);
                holding = amp < kRhythmicFillMinAmplitude;
                if (h >= kMinHalfPeriodMs && h <= kMaxHalfPeriodMs) half = h;
            }
            if (!holding && !(near.halves.size() >= 3) && global_amp < kRhythmicFillMinAmplitude) holding = true;
            ++gaps;
            if (holding) {
                // The same points as FillGaps: k = 1 .. dt / step - 1.
                const int64_t n = dt / line_step;
                for (int64_t k = 1; k < n; ++k) {
                    const int64_t t = prev.at + k * line_step;
                    if (t >= next.at) break;
                    const double f = double(t - prev.at) / double(dt);
                    out.push_back({t, ClampPos(std::lround(prev.pos + f * (next.pos - prev.pos)))});
                    ++added;
                }
                PushOrReplace(out, next);
                continue;
            }
            amp = std::clamp(amp, double(kRhythmicFillMinAmplitude), 80.0);
            // Turning times: uniform at the script's or the audio's tempo,
            // else the heart rhythm's beats and their midpoints.
            std::vector<double> turns;
            if (half <= 0 && audio_bpm > 0) half = 30000.0 / audio_bpm;
            if (half > 0) {
                for (double t = double(prev.at) + half; t <= double(next.at) - 0.5 * half; t += half) turns.push_back(t);
            } else {
                const auto beats = BaseBeatGrid(base, double(prev.at), double(next.at) + 2 * 60000.0 / kBaseBpmMin);
                for (size_t k = 1; k < beats.size(); ++k) {
                    const double mid = 0.5 * (beats[k - 1] + beats[k]);
                    const double h = 0.5 * (beats[k] - beats[k - 1]);
                    if (mid > double(next.at) - 0.5 * h) break;
                    turns.push_back(mid);
                    if (beats[k] > double(next.at) - 0.5 * h) break;
                    turns.push_back(beats[k]);
                }
            }
            int dir = LastDirection(a, i - 1);
            if (dir == 0) dir = prev.pos >= 50 ? 1 : -1;
            for (size_t k = 0; k < turns.size(); ++k) {
                const auto t = int64_t(std::llround(turns[k]));
                if (t <= out.back().at || t >= next.at) continue;
                const double f = double(t - prev.at) / double(dt);
                double pos = prev.pos + f * (next.pos - prev.pos);
                if (k % 2 == 0) pos -= dir * amp;  // away from the last movement, then back
                out.push_back({t, ClampPos(std::lround(pos))});
                ++added;
            }
            PushOrReplace(out, next);
        }
        a = std::move(out);
    }
    if (gaps_filled) *gaps_filled = gaps;
    if (points_added) *points_added = added;
    return a;
}

std::vector<Action> HealTrackingGaps(std::vector<Action> actions, const std::vector<TrackingGap>& gaps, int64_t step_ms,
                                     double audio_hz, int* windows_healed, int* points_added) {
    if (windows_healed) *windows_healed = 0;
    if (points_added) *points_added = 0;
    const auto merged = MergeGaps(gaps);
    if (actions.size() < 2 || merged.empty()) return actions;
    std::vector<Action> sorted = actions;
    SortByTime(sorted);
    const auto inside = [&](int64_t t) {
        for (const auto& g : merged)
            if (t > g.start_ms && t < g.end_ms) return true;
        return false;
    };
    const auto gapOf = [&](int64_t t) {
        for (size_t i = 0; i < merged.size(); ++i)
            if (t >= merged[i].start_ms && t <= merged[i].end_ms) return int(i);
        return -1;
    };
    std::vector<Action> kept;
    std::vector<bool> touched(merged.size(), false);
    int removed = 0;
    for (const auto& x : sorted) {
        if (inside(x.at)) {
            ++removed;
            const int g = gapOf(x.at);
            if (g >= 0) touched[size_t(g)] = true;
            continue;
        }
        kept.push_back(x);
    }
    if (kept.size() < 2) return actions;  // healing would destroy the script
    const int64_t step = std::max<int64_t>(20, StepOrDefault(step_ms, audio_hz));
    int added = 0;
    std::vector<Action> out;
    out.reserve(kept.size() + merged.size() * 8);
    out.push_back(kept.front());
    for (size_t i = 1; i < kept.size(); ++i) {
        const Action prev = out.back();
        const Action& next = kept[i];
        const int64_t dt = next.at - prev.at;
        if (dt > step) {
            for (int64_t k = 1;; ++k) {
                const int64_t t = prev.at + k * step;
                if (t >= next.at) break;
                const int g = gapOf(t);
                if (g < 0) continue;  // the healthy curve outside stays as it is
                const double f = double(t - prev.at) / double(dt);
                out.push_back({t, ClampPos(std::lround(prev.pos + f * (next.pos - prev.pos)))});
                ++added;
                touched[size_t(g)] = true;
            }
        }
        PushOrReplace(out, next);
    }
    const int healed = int(std::count(touched.begin(), touched.end(), true));
    if (healed == 0 && removed == 0 && added == 0) return actions;
    if (windows_healed) *windows_healed = healed;
    if (points_added) *points_added = added;
    return out;
}

ImproveResult ImproveScript(const std::vector<Action>& actions, const ImproveOptions& opts) {
    ImproveResult res;
    res.before_count = int(actions.size());
    if (actions.size() < 2) throw std::invalid_argument("funscript: improve needs at least 2 points");
    std::vector<Action> cur = actions;
    SortByTime(cur);

    if (opts.start_ms > 0 || opts.end_ms > 0) {
        auto trimmed = TrimActions(cur, opts.start_ms > 0 ? opts.start_ms : -1, opts.end_ms > 0 ? opts.end_ms : -1);
        res.trimmed = trimmed.size() != cur.size() || trimmed.front().at != cur.front().at ||
                      trimmed.back().at != cur.back().at;
        cur = std::move(trimmed);
    }

    if (opts.heal_tracking_gaps && !opts.tracking_gaps.empty()) {
        int windows = 0, pts = 0;
        auto healed = HealTrackingGaps(cur, opts.tracking_gaps, opts.step_ms, opts.audio_hz, &windows, &pts);
        if (windows > 0) {
            cur = std::move(healed);
            res.windows_healed = windows;
            res.points_added += pts;
            res.clear_tracking_gaps = true;
        }
    }

    if (opts.fill_gaps) {
        int64_t max_gap = opts.max_gap_ms > 0 ? opts.max_gap_ms : AutoFillGapThreshold(cur);
        // The spacing before filling: after filling, the median collapses.
        const double med = MedianSpacingMs(cur);
        const auto fill = [&opts](std::vector<Action> in, int64_t gap, int* g, int* p) {
            return opts.rhythmic_fill ? FillGapsRhythmic(std::move(in), gap, opts.step_ms, opts.audio_hz, opts.base_rhythm, g, p)
                                      : FillGaps(std::move(in), gap, opts.step_ms, opts.audio_hz, g, p);
        };
        int gaps = 0, pts = 0;
        cur = fill(std::move(cur), max_gap, &gaps, &pts);
        if (opts.max_gap_ms <= 0) {
            // Auto: a second pass for medium holes, never below ~4× the natural
            // stroke spacing (slow strokes must not become 100 ms ramps).
            int64_t tight = kDefaultFillGapMs / 2;
            if (med > 0) tight = std::max<int64_t>(tight, std::llround(med * 4));
            if (tight > 0 && tight < max_gap) {
                int g2 = 0, p2 = 0;
                cur = fill(std::move(cur), tight, &g2, &p2);
                gaps += g2;
                pts += p2;
                max_gap = tight;
            }
        }
        res.gaps_filled = gaps;
        res.points_added += pts;
        res.fill_gap_ms = max_gap;
        res.fill_step_ms = StepOrDefault(opts.step_ms, opts.audio_hz);
    }
    res.after_count = int(cur.size());
    res.actions = std::move(cur);
    return res;
}

}  // namespace samn
