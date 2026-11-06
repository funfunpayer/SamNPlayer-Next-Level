#include "samn/intensity_arc.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace samn {

namespace {

// Motion over twice the busy level is a glitch or a cut, not more action.
constexpr double kMotionCap = 2.0;
// A climax stands out against the action within half a minute on either
// side; lows farther away (the intro, black ends) say nothing about it.
constexpr int64_t kProminenceReachMs = 30000;
// Smaller swells of the arc are the action's own ebb and flow.
constexpr double kMinProminence = 0.05;
// The step from steady action to a clear climax (motion 2x, loud) is about 0.4.
constexpr double kFullConfidenceProminence = 0.4;
// Sound and motion topping out farther apart than this disagree.
constexpr int64_t kAgreementMs = 5000;
constexpr double kDisagreement = 0.7;
// The top of a climax: within a quarter of its stretch's range below the maximum.
constexpr double kTopBand = 0.25;
// An intro must sit clearly below the action: a smaller step is the action's ebb.
constexpr double kMinIntroStep = 0.2;
// A build-up has left the intro once it is a quarter of the way up to the action.
constexpr double kOnsetShare = 0.25;
// A build-up climbs at least 5 % of the intro-to-action step per build-up
// length; the arc after a plain step drifts by less (jitter).
constexpr double kBuildUpRise = 0.05;
// A step on the calm side at least half as big as the best one ends the intro earlier.
constexpr double kEarlierStep = 0.5;
// The lateness prior 0.7 + 0.3 t/T: a late climax is the likelier one, an early one still wins on height.
constexpr double kEarlyWeight = 0.7;

double Clean(double v) { return std::isfinite(v) && v > 0 ? v : 0.0; }

double Percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(std::floor(q * double(v.size() - 1) + 0.5)));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

// A centred moving average over `window` steps (shorter at the ends), from prefix sums.
std::vector<double> Smooth(const std::vector<double>& v, size_t window) {
    std::vector<double> prefix(v.size() + 1, 0.0);
    for (size_t i = 0; i < v.size(); ++i) prefix[i + 1] = prefix[i] + v[i];
    const size_t half = window / 2;
    std::vector<double> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const size_t a = i >= half ? i - half : 0, b = std::min(v.size(), i + half + 1);
        out[i] = (prefix[b] - prefix[a]) / double(b - a);
    }
    return out;
}

// The top of curve[a, b): the samples within kTopBand of the range below its
// maximum, their span and their centroid weighted by the height over that level.
struct Top {
    size_t first, last;  // [first, last)
    double centre;
};
Top TopOf(const std::vector<double>& curve, size_t a, size_t b) {
    const auto [lo, hi] = std::minmax_element(curve.begin() + std::ptrdiff_t(a), curve.begin() + std::ptrdiff_t(b));
    const double level = *hi - kTopBand * (*hi - *lo);
    Top top{b, a, 0};
    double sum = 0, weight = 0;
    for (size_t i = a; i < b; ++i) {
        if (curve[i] < level) continue;
        top.first = std::min(top.first, i);
        top.last = i + 1;
        sum += double(i) * (curve[i] - level);
        weight += curve[i] - level;
    }
    top.centre = weight > 0 ? sum / weight : 0.5 * double(top.first + top.last - 1);
    return top;
}

// A build-up from i on: the arc keeps climbing, `rise` or more over every
// stretch of `length` steps that starts within the first `length` steps.
bool BuildsUp(const std::vector<double>& arc, size_t i, size_t length, double rise) {
    if (i + 2 * length > arc.size()) return false;
    for (size_t k = i; k < i + length; ++k)
        if (arc[k + length] - arc[k] < rise) return false;
    return true;
}

// The best split of [0, end) (prefix sums) into a lower and a higher mean,
// each side at least min_len long; at = 0 when there is none.
struct Step {
    size_t at = 0;
    double low = 0, high = 0;
};
Step BestStep(const std::vector<double>& prefix, size_t end, size_t min_len) {
    Step step;
    double best = 0;
    for (size_t k = min_len; k + min_len <= end; ++k) {
        const double left = prefix[k] / double(k), right = (prefix[end] - prefix[k]) / double(end - k);
        const double gain = double(k) * double(end - k) * (right - left) * (right - left);
        if (right > left && gain > best) {
            best = gain;
            step = {k, left, right};
        }
    }
    return step;
}

// The intro end: the step up from a calm start to the action within the
// first intro_share of the clip (a two-segment mean change point), placed
// where the action first stays above the midpoint of the two levels for
// `steady` steps. When the action starts with a build-up, the midpoint is
// reached late: the intro then ends where the rise leaves the intro's level.
size_t IntroEnd(const std::vector<double>& detail, const std::vector<double>& arc, double share, size_t steady,
                size_t build_up) {
    const size_t n = detail.size(), m = std::min(n, size_t(share * double(n)));
    if (m < 2 * steady) return 0;
    // A climax inside the window must not look like the step the intro ends with.
    const double cap = Percentile(detail, 0.5);
    std::vector<double> prefix(m + 1, 0.0);
    for (size_t i = 0; i < m; ++i) prefix[i + 1] = prefix[i] + std::min(cap, detail[i]);
    Step step = BestStep(prefix, m, steady);
    if (step.at == 0 || step.high - step.low < kMinIntroStep * step.high) return 0;
    // A slow rise pulls the best split into itself; a clear step on the calm
    // side before it is where the intro really ends.
    for (Step earlier; (earlier = BestStep(prefix, step.at, steady)).at > 0 &&
                       earlier.high - earlier.low >= kEarlierStep * (step.high - step.low);)
        step = earlier;
    const double midpoint = 0.5 * (step.low + step.high);
    size_t end = step.at;
    for (size_t i = 0, run = 0; i < n && i < m + run; ++i) {
        run = std::min(cap, detail[i]) >= midpoint ? run + 1 : 0;
        if (run >= steady) {
            end = i + 1 - run;
            break;
        }
    }
    if (BuildsUp(arc, end, build_up, kBuildUpRise * (step.high - step.low))) {
        const double onset = step.low + kOnsetShare * (step.high - step.low);
        while (end > 0 && detail[end - 1] > onset) --end;
    }
    return end;
}

// The local maxima of the arc from `from` on; a plateau counts once, at its middle.
std::vector<size_t> LocalMaxima(const std::vector<double>& arc, size_t from) {
    std::vector<size_t> out;
    const size_t n = arc.size();
    for (size_t i = from; i < n;) {
        size_t j = i;
        while (j + 1 < n && arc[j + 1] == arc[i]) ++j;
        const bool left = i == 0 || arc[i - 1] < arc[i], right = j + 1 == n || arc[j + 1] < arc[i];
        if (left && right && !(i == 0 && j + 1 == n)) out.push_back((i + j) / 2);
        i = j + 1;
    }
    return out;
}

// Topographic prominence within `reach` steps: the height over the higher of
// the two lowest points before the curve rises above the peak on each side.
// A side that is the clip's edge does not count.
double Prominence(const std::vector<double>& arc, size_t p, size_t reach) {
    double left = arc[p], right = arc[p];
    bool has_left = false, has_right = false;
    for (size_t i = p; i > 0 && p - i < reach && arc[i - 1] <= arc[p]; --i) {
        left = std::min(left, arc[i - 1]);
        has_left = true;
    }
    for (size_t i = p + 1; i < arc.size() && i - p <= reach && arc[i] <= arc[p]; ++i) {
        right = std::min(right, arc[i]);
        has_right = true;
    }
    if (!has_left && !has_right) return 0;
    const double base = !has_left ? right : !has_right ? left : std::max(left, right);
    return arc[p] - base;
}

}  // namespace

IntensityArc ComputeArc(const PhaseSignals& s, const ArcOptions& o) {
    IntensityArc out;
    out.step_ms = s.step_ms;
    const size_t n = !s.motion.empty() ? s.motion.size() : s.loudness.size();
    if (n == 0 || s.step_ms <= 0) return out;
    const auto steps = [&](int64_t ms) { return std::max<size_t>(1, size_t((ms + s.step_ms - 1) / s.step_ms)); };

    // The inputs, cleaned; an input that is short or all 0 is missing.
    const auto input = [n](const std::vector<double>& v) {
        std::vector<double> c;
        if (v.size() < n) return c;
        c.resize(n);
        for (size_t i = 0; i < n; ++i) c[i] = Clean(v[i]);
        if (*std::max_element(c.begin(), c.end()) <= 0) c.clear();
        return c;
    };
    std::vector<double> motion = input(s.motion), loud = input(s.loudness);
    if (!motion.empty()) {
        // Against the busy level of the moving part: black stretches do not lower it.
        std::vector<double> moving;
        for (double v : motion)
            if (v > 0) moving.push_back(v);
        const double busy = Percentile(std::move(moving), 0.9);
        for (auto& v : motion) v = std::min(kMotionCap, v / busy);
    }
    for (auto& v : loud) v = std::min(1.0, v);
    const double wm = motion.empty() ? 0 : loud.empty() ? 1 : o.motion_weight;
    const double wl = loud.empty() ? 0 : motion.empty() ? 1 : o.loudness_weight;
    std::vector<double> intensity(n, 0.0);
    for (size_t i = 0; i < n; ++i)
        intensity[i] = (motion.empty() ? 0 : wm * motion[i]) + (loud.empty() ? 0 : wl * loud[i]);
    out.detail = Smooth(intensity, steps(o.detail_ms));
    out.arc = Smooth(intensity, steps(o.arc_ms));
    if (motion.empty() && loud.empty()) return out;

    const size_t intro = IntroEnd(out.detail, out.arc, o.intro_share, steps(o.steady_ms), steps(o.build_up_ms));
    out.intro_end_ms = int64_t(intro) * s.step_ms;

    // Climax candidates over the whole clip after the intro.
    const double median = Percentile(std::vector<double>(out.arc.begin() + std::ptrdiff_t(intro), out.arc.end()), 0.5);
    const auto motion_detail = Smooth(motion, steps(o.detail_ms)), loud_detail = Smooth(loud, steps(o.detail_ms));
    const double duration = double(n) * double(s.step_ms);
    struct Candidate {
        ArcPeak peak;
        double score;
    };
    std::vector<Candidate> candidates;
    for (size_t p : LocalMaxima(out.arc, intro)) {
        const double prominence = Prominence(out.arc, p, steps(kProminenceReachMs));
        if (prominence < kMinProminence || out.arc[p] < o.min_gain * median) continue;
        // The climax's stretch: where the detail stays above half the prominence.
        const double level = out.arc[p] - 0.5 * prominence;
        size_t a = p, b = p + 1;
        while (a > 0 && out.detail[a - 1] >= level) --a;
        while (b < n && out.detail[b] >= level) ++b;
        const Top top = TopOf(out.detail, a, b);
        ArcPeak peak;
        peak.t_ms = std::llround(top.centre * double(s.step_ms));
        peak.start_ms = int64_t(top.first) * s.step_ms;
        peak.end_ms = int64_t(top.last) * s.step_ms;
        peak.prominence = prominence;
        peak.confidence = std::clamp(prominence / kFullConfidenceProminence, 0.0, 1.0);
        if (!motion.empty() && !loud.empty()) {
            const double apart = std::abs(TopOf(motion_detail, a, b).centre - TopOf(loud_detail, a, b).centre);
            if (apart * double(s.step_ms) > double(kAgreementMs)) peak.confidence *= kDisagreement;
        }
        const double lateness = kEarlyWeight + (1 - kEarlyWeight) * double(peak.t_ms) / duration;
        candidates.push_back({peak, prominence * lateness});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& x, const Candidate& y) { return x.score > y.score; });

    // The primary, then the next ones that are high enough and far enough from every one taken.
    for (const auto& c : candidates) {
        if (!out.peaks.empty()) {
            if (int(out.peaks.size()) > o.max_secondary) break;
            if (c.peak.prominence < o.secondary_share * out.peaks.front().prominence) continue;
            const bool apart = std::all_of(out.peaks.begin(), out.peaks.end(), [&](const ArcPeak& q) {
                return std::llabs(q.t_ms - c.peak.t_ms) >= o.secondary_gap_ms;
            });
            if (!apart) continue;
        }
        out.peaks.push_back(c.peak);
    }
    return out;
}

}  // namespace samn
