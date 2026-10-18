#include "samn/motion_fidelity.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>

namespace samn {

namespace {

std::vector<Action> Sorted(std::vector<Action> v) {
    std::stable_sort(v.begin(), v.end(), [](const Action& x, const Action& y) { return x.at < y.at; });
    return v;
}

// numpy.interp: linear between knots, clamped outside.
double InterpClamp(const std::vector<Action>& s, double x) {
    if (s.empty()) return 0;
    if (x <= s.front().at) return s.front().pos;
    if (x >= s.back().at) return s.back().pos;
    const auto it = std::upper_bound(s.begin(), s.end(), x, [](double v, const Action& a) { return v < a.at; });
    const Action& a1 = *it;
    const Action& a0 = *(it - 1);
    if (a1.at == a0.at) return a0.pos;
    const double w = (x - a0.at) / double(a1.at - a0.at);
    return a0.pos + w * (a1.pos - a0.pos);
}

std::vector<double> Resample(const std::vector<Action>& s, int64_t t0, int64_t t1, int step) {
    std::vector<double> out;
    for (int64_t t = t0; t <= t1; t += step) out.push_back(InterpClamp(s, double(t)));
    return out;
}

double Mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return s / v.size();
}

double PopStd(const std::vector<double>& v, double m) {
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / v.size());
}

std::optional<double> ShapeError(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() < 2 || a.size() != b.size()) return std::nullopt;
    const double ma = Mean(a), mb = Mean(b), sa = PopStd(a, ma), sb = PopStd(b, mb);
    if (sa < 1e-9 || sb < 1e-9) return std::nullopt;
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i) sum += std::abs((a[i] - ma) / sa - (b[i] - mb) / sb);
    return sum / a.size();
}

std::vector<Action> InRange(const std::vector<Action>& s, int64_t t0, int64_t t1) {
    std::vector<Action> out;
    for (const Action& a : s)
        if (a.at >= t0 && a.at <= t1) out.push_back(a);
    return out;
}

void AnnotateAliasing(LagCorrelation& best, const std::map<int, double>& lag_scores, const std::vector<Action>& ref,
                      int resample_step, int max_lag, int lag_step) {
    if (lag_scores.size() < 2) return;
    const int period = DominantPeriodMs(ref, resample_step);
    if (period < kMinDominantPeriodMs) return;
    best.dominant_period_ms = period;
    const int tolerance = lag_step < 1 ? kDefaultLagStepMs : lag_step;
    std::vector<int> alts;
    for (int k = 1; k * period <= max_lag * 2; ++k)
        for (int sign : {1, -1}) {
            const int target = best.lag_ms + sign * k * period;
            if (target < -max_lag || target > max_lag) continue;
            for (const auto& [lag, r] : lag_scores)
                if (lag != best.lag_ms && std::abs(lag - target) <= tolerance && best.r - r <= kDefaultAliasingEpsilon)
                    alts.push_back(lag);
        }
    if (alts.empty()) return;
    std::sort(alts.begin(), alts.end());
    alts.erase(std::unique(alts.begin(), alts.end()), alts.end());
    best.alternate_lags_ms = alts;
    best.aliasing_risk = true;
}

}  // namespace

std::optional<double> Pearson(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() < 2 || a.size() != b.size()) return std::nullopt;
    const double ma = Mean(a), mb = Mean(b);
    double saa = 0, sbb = 0, sab = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
        sab += (a[i] - ma) * (b[i] - mb);
    }
    const double n = double(a.size()), sa = std::sqrt(saa / n), sb = std::sqrt(sbb / n);
    if (sa < 1e-9 || sb < 1e-9) return std::nullopt;
    return sab / (n * sa * sb);
}

std::optional<LagCorrelation> BestLagCorrelation(const std::vector<Action>& a_in, const std::vector<Action>& b_in,
                                                 int max_lag, int lag_step, int step) {
    if (max_lag < 0) max_lag = kDefaultMaxLagMs;
    if (lag_step <= 0) lag_step = kDefaultLagStepMs;
    if (step <= 0) step = kDefaultResampleStepMs;
    if (a_in.size() < 2 || b_in.size() < 2) return std::nullopt;
    const std::vector<Action> a = Sorted(a_in), b = Sorted(b_in);

    std::optional<LagCorrelation> best;
    std::optional<double> zero_lag_r;
    std::map<int, double> lag_scores;  // best r per lag, for the aliasing flag only
    for (int lag = -max_lag; lag <= max_lag; lag += lag_step) {
        const int64_t start = std::max(a.front().at, b.front().at + lag);
        const int64_t end = std::min(a.back().at, b.back().at + lag);
        if (end <= start || (end - start) / step + 1 < kMinOverlapSamples) continue;
        const std::vector<double> sa = Resample(a, start, end, step);
        std::vector<Action> shifted = b;
        for (Action& x : shifted) x.at += lag;
        const std::vector<double> sb = Resample(shifted, start, end, step);
        std::vector<double> inv(sb.size());
        std::transform(sb.begin(), sb.end(), inv.begin(), [](double v) { return 100.0 - v; });
        const std::optional<double> r_normal = Pearson(sa, sb);
        const std::optional<double> r_inverted = r_normal ? Pearson(sa, inv) : std::nullopt;
        const std::pair<const std::optional<double>*, const char*> cands[] = {{&r_normal, "normal"},
                                                                              {&r_inverted, "inverted"}};
        for (const auto& [r, orientation] : cands) {
            if (!*r) continue;
            auto [it, fresh] = lag_scores.emplace(lag, **r);
            if (!fresh && **r > it->second) it->second = **r;
            if (!best || **r > best->r) {
                LagCorrelation c;
                c.r = **r;
                c.lag_ms = lag;
                c.orientation = orientation;
                c.n_samples = static_cast<int>(sa.size());
                c.shape_error = ShapeError(sa, std::string(orientation) == "normal" ? sb : inv);
                best = c;
            }
        }
        if (lag == 0 && r_normal) zero_lag_r = r_normal;
    }
    if (!best) return std::nullopt;
    best->r_zero_lag = zero_lag_r;
    best->low_confidence = best->n_samples < kLowConfidenceSamples;
    AnnotateAliasing(*best, lag_scores, a, step, max_lag, lag_step);
    return best;
}

int DominantPeriodMs(const std::vector<Action>& in, int step) {
    if (in.size() < 4 || step <= 0) return 0;
    const std::vector<Action> s = Sorted(in);
    if (s.back().at - s.front().at < kMinDominantPeriodMs * 2) return 0;
    const std::vector<double> series = Resample(s, s.front().at, s.back().at, step);
    const int n = static_cast<int>(series.size());
    if (n < 16) return 0;
    const double m = Mean(series);
    std::vector<double> c(n);
    double denom = 0;
    for (int i = 0; i < n; ++i) {
        c[i] = series[i] - m;
        denom += c[i] * c[i];
    }
    if (denom < 1e-9) return 0;
    // Round the floor up: integer division alone searched below it (150/40 -> 120 ms).
    int min_lag = std::max(1, kMinDominantPeriodMs / step);
    if (min_lag * step < kMinDominantPeriodMs) ++min_lag;
    int max_lag = kMaxDominantPeriodMs / step;
    if (max_lag >= n / 2) max_lag = n / 2 - 1;
    if (max_lag <= min_lag) return 0;
    std::vector<double> acs;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        double num = 0;
        for (int i = 0; i + lag < n; ++i) num += c[i] * c[i + lag];
        acs.push_back(num / denom);
    }
    // A smooth curve's autocorrelation falls from the smallest lag anyway;
    // only a genuine local peak after that first decline is a period.
    int best_lag = 0;
    double best_ac = 0;
    bool past_decline = false;
    for (size_t idx = 0; idx < acs.size(); ++idx) {
        if (idx > 0 && acs[idx] > acs[idx - 1]) past_decline = true;
        const bool local_max = idx > 0 && idx + 1 < acs.size() && acs[idx] > acs[idx - 1] && acs[idx] >= acs[idx + 1];
        if (past_decline && local_max && acs[idx] > best_ac) {
            best_ac = acs[idx];
            best_lag = min_lag + static_cast<int>(idx);
        }
    }
    if (best_ac < 0.25 || best_lag == 0) return 0;
    return best_lag * step;
}

std::vector<WindowResult> WindowedBestLagCorrelation(const std::vector<Action>& a_in, const std::vector<Action>& b_in,
                                                     int window_ms, int max_lag, int lag_step, int step) {
    if (window_ms <= 0) window_ms = kDefaultWindowMs;
    if (max_lag < 0) max_lag = kDefaultMaxLagMs;
    std::vector<WindowResult> out;
    if (a_in.size() < 2) return out;
    const std::vector<Action> a = Sorted(a_in), b = Sorted(b_in);
    for (int64_t start = a.front().at; start < a.back().at;) {
        const int64_t end = std::min<int64_t>(start + window_ms, a.back().at);
        WindowResult row;
        row.start_ms = start;
        row.end_ms = end;
        const std::vector<Action> as = InRange(a, start, end), bs = InRange(b, start - max_lag, end + max_lag);
        if (as.size() < 2 || bs.size() < 2) {
            row.reason = "too_few_actions_in_window";
        } else {
            row.correlation = BestLagCorrelation(as, bs, max_lag, lag_step, step);
            if (!row.correlation) row.reason = "undefined_correlation";
        }
        out.push_back(row);
        start = end;
    }
    return out;
}

const char* PhaseVerdictName(PhaseVerdict v) {
    switch (v) {
        case PhaseVerdict::kTiming: return "timing";
        case PhaseVerdict::kShape: return "shape";
        case PhaseVerdict::kOk: return "ok";
        default: return "undefined";
    }
}

PhaseDiagnosis DiagnosePhase(const std::optional<LagCorrelation>& corr, double gain_min, double min_r) {
    if (gain_min <= 0) gain_min = kDefaultTimingGainMin;
    if (min_r <= 0) min_r = kDefaultMinAlignedR;
    if (!corr) return {PhaseVerdict::kUndefined, "no usable overlap or a constant series (Pearson undefined)"};
    if (corr->r < min_r)
        return {PhaseVerdict::kShape, "aligned correlation stays weak: check perception, normalisation, box"};
    if (corr->r_zero_lag && corr->r - *corr->r_zero_lag >= gain_min)
        return {PhaseVerdict::kTiming, "the curve fits after lag alignment: fix timing / phase first"};
    return {PhaseVerdict::kOk, "raw and aligned correlation are both usable"};
}

}  // namespace samn
