#include "samn/intensity_arc.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace samn {

namespace {

// Motion over twice the busy level is a glitch or a cut, not more action.
constexpr double kMotionCap = 2.0;
// Motion spikes shorter than about half of this many steps are cuts or
// flashes: the running median's width (2.5 s at 500 ms steps).
constexpr size_t kCutMedian = 5;
// A climax stands out against the action within half a minute on either side
// of its top; lows farther away (the intro, black ends) say nothing about it.
constexpr int64_t kProminenceReachMs = 30000;
// Smaller swells of the arc are the action's own ebb and flow.
constexpr double kMinProminence = 0.05;
// Confidence ranks climaxes and is no probability: a climax twice as busy and
// loud as the rest of the action (strength 1) has the full 1.
constexpr double kFullStrength = 1.0;
// One input's level on a climax counts at most this many times its action
// level (an input near 0 elsewhere must not outweigh the other one).
constexpr double kMaxRatio = 4.0;
// Sound and motion topping out farther apart than this disagree.
constexpr int64_t kAgreementMs = 5000;
constexpr double kDisagreement = 0.7;
// The top of a climax: within half its stretch's range below the maximum.
constexpr double kTopBand = 0.5;
// An intro must sit clearly below the action: a smaller step is the action's ebb.
constexpr double kMinIntroStep = 0.2;
// A build-up has left the intro once it is a quarter of the way up to the action.
constexpr double kOnsetShare = 0.25;
// A build-up climbs at least 5 % of the intro-to-action step per build-up
// length; the arc after a plain step drifts by less (jitter).
constexpr double kBuildUpRise = 0.05;
// The action's level for the intro search: the 65th percentile of the detail.
constexpr double kActionLevel = 0.65;
// An intro is calm: one input at most half its level in the action after it.
constexpr double kIntroCalm = 0.5;
// A step on the calm side at least 0.4 of the best one ends the intro earlier
// (the step from the intro to the action before an early plateau); a black
// start before a calm intro is a smaller step and stays inside the intro.
constexpr double kEarlierStep = 0.4;
// Drift is no climax: a climax stands this many times over the action beside
// it (per input, the lower of the medians over 60 s on either side within
// the action), unless one of its edges is sudden: then climax_gain is
// enough. A sudden edge goes from a quarter to three quarters of the rise
// within kSuddenMs (a step, a burst, a drop); drift rises slowly. Both fitted
// on the bench's train families only (sweep in
// docs/benchmarks/INTENSITY_ARC_2026-11-06.md).
constexpr double kFlankGain = 1.6;
constexpr int64_t kSuddenMs = 3000;
// A sudden edge must rise this many times the clip's own detail noise (see
// `noise` in ComputeArc), or jitter would look sudden. Fitted on the train
// families.
constexpr double kSuddenNoise = 6.0;
// A sudden rise held to the clip's end (the top step of a staircase) counts
// from this gain over the step before. Fitted on the train families.
constexpr double kLastStepGain = 1.15;
constexpr int64_t kFlankMs = 60000;
// A clip is calm outside its climaxes when the spread of that action is at
// most this share of the primary climax's rise. Fitted on the train families.
constexpr double kCalmSpread = 1.0;
// The time scales hold for clips of two minutes and more; shorter clips scale
// them down, or a climax of a 30 s clip would be smoothed away.
constexpr int64_t kFullScaleMs = 120000;
// The lateness prior 0.7 + 0.3 t/T: a late climax is the likelier one, an early one still wins on height.
constexpr double kEarlyWeight = 0.7;

double Clean(double v) { return std::isfinite(v) && v > 0 ? v : 0.0; }

double Percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(std::floor(q * double(v.size() - 1) + 0.5)));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

// The median of `sorted` without the values of `part`, a sub-multiset of it
// (Percentile's rank); -1 when nothing is left. One binary search over the
// sorted values instead of a copy per call.
double MedianWithout(const std::vector<double>& sorted, std::vector<double> part) {
    if (part.size() >= sorted.size()) return -1;
    std::sort(part.begin(), part.end());
    const size_t k = size_t(std::floor(0.5 * double(sorted.size() - part.size() - 1) + 0.5));
    const auto rank = [&](double x) {  // how many of the rest are <= x
        return size_t(std::upper_bound(sorted.begin(), sorted.end(), x) - sorted.begin()) -
               size_t(std::upper_bound(part.begin(), part.end(), x) - part.begin());
    };
    size_t lo = 0, hi = sorted.size() - 1;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (rank(sorted[mid]) >= k + 1) hi = mid;
        else lo = mid + 1;
    }
    return sorted[lo];
}

// The mean of v[a, b).
double MeanOf(const std::vector<double>& v, size_t a, size_t b) {
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += v[i];
    return b > a ? sum / double(b - a) : 0;
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

// The run [first, last) around a sample, grown each way within [a, b) to the
// next sample at or above `level` across a dip that is shorter than `gap` or
// never below `level - 2 * noise` (jitter); its ends stay at the level.
void GrowRun(const std::vector<double>& curve, size_t a, size_t b, double level, size_t gap, double noise,
             size_t& first, size_t& last) {
    const double shallow = level - 2 * noise;
    for (;;) {
        size_t j = first;
        bool deep = false;
        while (j > a && curve[j - 1] < level && !(deep && first - j > gap)) deep |= curve[--j] < shallow;
        if (j == a || curve[j - 1] < level) break;
        first = j - 1;
    }
    for (;;) {
        size_t j = last;
        bool deep = false;
        while (j < b && curve[j] < level && !(deep && j - last > gap)) deep |= curve[j++] < shallow;
        if (j == b || curve[j] < level) break;
        last = j + 1;
    }
}

// The top of curve[a, b): the run around its maximum that stays within
// kTopBand of the range below it (GrowRun), and the run's middle. Two bursts
// with a real dip between are two tops, and the middle of a long plateau is
// steadier than any weighted centre.
struct Top {
    size_t first, last;  // [first, last)
    double centre;
};
Top TopOf(const std::vector<double>& curve, size_t a, size_t b, size_t gap, double noise) {
    const auto begin = curve.begin();
    const auto [lo, hi] = std::minmax_element(begin + std::ptrdiff_t(a), begin + std::ptrdiff_t(b));
    Top top{size_t(hi - begin), size_t(hi - begin) + 1, 0};
    GrowRun(curve, a, b, *hi - kTopBand * (*hi - *lo), gap, noise, top.first, top.last);
    top.centre = 0.5 * double(top.first + top.last - 1);
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
size_t IntroEnd(const std::vector<double>& detail, const std::vector<double>& arc, const std::vector<double>& motion,
                const std::vector<double>& loud, double share, size_t steady, size_t build_up) {
    const size_t n = detail.size(), m = std::min(n, size_t(share * double(n)));
    if (m < 2 * steady) return 0;
    // A climax inside the window must not look like the step the intro ends
    // with: values are capped at the action's level. The 65th percentile, not
    // the median: a long calm end (a cool-down) would pull the median down to
    // its own level and flatten the action too.
    const double cap = Percentile(detail, kActionLevel);
    std::vector<double> prefix(m + 1, 0.0);
    for (size_t i = 0; i < m; ++i) prefix[i + 1] = prefix[i] + std::min(cap, detail[i]);
    Step step = BestStep(prefix, m, steady);
    if (step.at == 0 || step.high - step.low < kMinIntroStep * step.high) return 0;
    // A slow rise pulls the best split into itself; a clear step on the calm
    // side before it is where the intro really ends.
    for (Step earlier; (earlier = BestStep(prefix, step.at, steady)).at > 0 &&
                       earlier.high - earlier.low >= kEarlierStep * (step.high - step.low);)
        step = earlier;
    // An intro is calm: the picture's motion or the sound is at most half its
    // level in the action after it (a staircase's first step is no intro).
    const auto calm = [&](const std::vector<double>& x) {
        return !x.empty() && MeanOf(x, 0, step.at) <= kIntroCalm * MeanOf(x, step.at, m);
    };
    if (!calm(motion) && !calm(loud)) return 0;
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

// Whether the detail climbs from a quarter to three quarters of the way from
// `base` up to `peak` within `limit` steps on the side of the top run that
// starts at `edge` and walks by `dir` (-1 left, +1 right), inside [from, to).
bool SuddenEdge(const std::vector<double>& detail, size_t edge, int dir, size_t from, size_t to, double base,
                double peak, size_t limit) {
    const double high = base + 0.75 * (peak - base), low = base + 0.25 * (peak - base);
    const auto next = [&](size_t i) { return dir < 0 ? (i > from ? i - 1 : to) : (i + 1 < to ? i + 1 : to); };
    size_t i = edge;
    while (i != to && detail[i] >= high) i = next(i);
    size_t j = i;
    for (size_t k = 0; j != to && detail[j] >= low && k <= limit; ++k) j = next(j);
    return i != to && j != to && detail[j] < low && (dir < 0 ? i - j : j - i) <= limit;
}

// The values of detail[from, to) that are not empty (live).
std::vector<double> LiveOf(const std::vector<double>& detail, const std::vector<bool>& live, size_t from, size_t to) {
    std::vector<double> v;
    for (size_t i = from; i < to; ++i)
        if (live[i]) v.push_back(detail[i]);
    return v;
}

// The median of the live part of detail[from, to), or -1 when that is
// shorter than min_len.
double FlankMedian(const std::vector<double>& detail, const std::vector<bool>& live, size_t from, size_t to,
                   size_t min_len) {
    std::vector<double> v = LiveOf(detail, live, from, std::max(from, to));
    if (v.empty() || v.size() < min_len) return -1;
    return Percentile(std::move(v), 0.5);
}

// How far one input rises on a climax: the mean of its detail over the top
// run against its own level over the rest of the action (the median of the
// action after the intro without the climax's stretch [a, b)) and against
// the action beside it (the lower median of `flank` steps on either side,
// within the action; a side shorter than a quarter of that does not count).
// Empty samples (no motion, no sound: black or still and silent) are no
// part of the action and count nowhere.
struct Rise {
    double over_rest = 1, over_flank = 1;
};
Rise RiseOf(const std::vector<double>& detail, const std::vector<bool>& live, const std::vector<double>& sorted_action,
            size_t intro, size_t a, size_t b, size_t top_first, size_t top_last, size_t flank) {
    const double top = MeanOf(detail, top_first, top_last);
    const auto ratio = [&](double level) { return level > 0 ? std::min(kMaxRatio, top / level) : top > 0 ? kMaxRatio : 1.0; };
    Rise rise;
    const double rest = MedianWithout(sorted_action, LiveOf(detail, live, a, b));
    if (rest >= 0) rise.over_rest = ratio(rest);
    const double left = FlankMedian(detail, live, std::max(intro, a - std::min(a, flank)), a, flank / 4);
    const double right = FlankMedian(detail, live, b, std::min(detail.size(), b + flank), flank / 4);
    const double low = left < 0 ? right : right < 0 ? left : std::min(left, right);
    if (low >= 0) rise.over_flank = ratio(low);
    return rise;
}

// Topographic prominence: the height over the higher of the lowest points on
// either side before the curve rises above the peak, within the action
// [from, size). A side walks on while it stays within `top` of the peak (a
// long plateau is one top), then at most `reach` steps further. A side that
// runs into the clip's end or the action's start does not count (the curve
// flattens at an edge, and the intro is no part of the action); when neither
// side stops inside, the lower one counts.
double ProminenceOf(const std::vector<double>& arc, size_t p, size_t from, size_t reach, double top) {
    struct Side {
        double low;
        bool inside;
    };
    const auto walk = [&](bool left) {
        Side side{arc[p], false};
        size_t off_top = 0;
        for (size_t i = p;;) {
            if (left ? i <= from : i + 1 == arc.size()) return side;
            i = left ? i - 1 : i + 1;
            if (arc[i] > arc[p]) break;
            side.low = std::min(side.low, arc[i]);
            if (arc[i] < arc[p] - top && ++off_top >= reach) break;
        }
        side.inside = true;
        return side;
    };
    const Side l = walk(true), r = walk(false);
    const double base = l.inside && r.inside ? std::max(l.low, r.low)
                        : l.inside           ? l.low
                        : r.inside           ? r.low
                                             : std::min(l.low, r.low);
    return arc[p] - base;
}

// Whether the peak at p is the clip's opening: walking left, the arc never
// falls more than `band` below it (higher samples on the way are allowed)
// before the clip's start, so nothing shows a real rise to it.
bool OpensAtTop(const std::vector<double>& arc, size_t p, double band) {
    for (size_t i = p; i-- > 0;)
        if (arc[i] < arc[p] - band) return false;
    return true;
}

}  // namespace

double ArcScale(int64_t duration_ms) { return std::clamp(double(duration_ms) / double(kFullScaleMs), 0.0, 1.0); }

IntensityArc ComputeArc(const PhaseSignals& s, const ArcOptions& o) {
    IntensityArc out;
    out.step_ms = s.step_ms;
    const size_t n = !s.motion.empty() ? s.motion.size() : s.loudness.size();
    if (n == 0 || s.step_ms <= 0) return out;
    const auto steps = [&](int64_t ms) {
        const int64_t m = std::clamp<int64_t>(ms, 0, std::numeric_limits<int64_t>::max() - s.step_ms);
        return std::max<size_t>(1, size_t((m + s.step_ms - 1) / s.step_ms));
    };
    const double duration = double(n) * double(s.step_ms);
    const double scale = ArcScale(int64_t(duration));
    const auto scaled = [&](int64_t ms) { return steps(std::llround(double(ms) * scale)); };

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
    if (motion.size() >= kCutMedian) {
        // A scene cut or a flash is a frame or two of motion, no action: a
        // running median over kCutMedian steps takes such spikes out (also
        // two close together) and leaves anything that lasts. Near the clip's
        // ends the window is the part inside the clip, and of an even count
        // the lower middle value counts.
        std::vector<double> m = motion;
        const size_t half = kCutMedian / 2;
        for (size_t i = 0; i < m.size(); ++i) {
            const size_t from = i < half ? 0 : i - half, to = std::min(m.size(), i + half + 1);
            std::array<double, kCutMedian> window{};
            std::copy(motion.begin() + std::ptrdiff_t(from), motion.begin() + std::ptrdiff_t(to), window.begin());
            const std::ptrdiff_t count = std::ptrdiff_t(to - from), mid = (count - 1) / 2;
            std::nth_element(window.begin(), window.begin() + mid, window.begin() + count);
            m[i] = window[size_t(mid)];
        }
        motion = std::move(m);
    }
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
    // Empty samples (neither motion nor sound) count in no statistic of the
    // action; an input that is exactly 0 at a sample is absent there and
    // counts in none of its own.
    std::vector<bool> live(n), motion_live(n, false), loud_live(n, false);
    for (size_t i = 0; i < n; ++i) {
        live[i] = intensity[i] > 0;
        if (!motion.empty()) motion_live[i] = motion[i] > 0;
        if (!loud.empty()) loud_live[i] = loud[i] > 0;
    }
    out.detail = Smooth(intensity, scaled(o.detail_ms));
    out.arc = Smooth(intensity, scaled(o.arc_ms));
    if (motion.empty() && loud.empty()) return out;

    const auto motion_detail = Smooth(motion, scaled(o.detail_ms)), loud_detail = Smooth(loud, scaled(o.detail_ms));
    const size_t intro = IntroEnd(out.detail, out.arc, motion_detail, loud_detail, o.intro_share, steps(o.steady_ms),
                                  scaled(o.build_up_ms));
    out.intro_end_ms = int64_t(intro) * s.step_ms;

    // Climax candidates over the whole action after the intro.
    const auto sorted_action = [&](const std::vector<double>& d, const std::vector<bool>& mask) {
        std::vector<double> v = d.empty() ? d : LiveOf(d, mask, std::min(intro, n), n);
        std::sort(v.begin(), v.end());
        return v;
    };
    const auto motion_action = sorted_action(motion_detail, motion_live);
    const auto loud_action = sorted_action(loud_detail, loud_live);
    const int64_t gap_ms = std::llround(double(o.secondary_gap_ms) * scale);
    const size_t half_arc = scaled(o.arc_ms) / 2, flank = scaled(kFlankMs);
    const size_t gap = scaled(o.detail_ms), sudden_steps = scaled(kSuddenMs);
    // The detail's own noise: the median second difference over one detail
    // window (a ramp's slope is no noise), scaled by 1 / sqrt(3) to the
    // median first difference of white noise.
    double noise = 0;
    {
        std::vector<double> bends;
        for (size_t i = intro + gap; i + gap < n; ++i)
            if (live[i - gap] && live[i] && live[i + gap])
                bends.push_back(std::abs(out.detail[i - gap] - 2 * out.detail[i] + out.detail[i + gap]));
        noise = Percentile(std::move(bends), 0.5) / std::sqrt(3.0);
    }
    struct Candidate {
        ArcPeak peak;
        double strength, score, top_level;
        size_t a, b;  // its stretch
    };
    std::vector<Candidate> candidates;
    for (size_t p : LocalMaxima(out.arc, intro)) {
        const double prominence = ProminenceOf(out.arc, p, intro, scaled(kProminenceReachMs), kMinProminence);
        // A clip that opens at its top has an opening (a cold open, a loud
        // title) or was cut inside the climax: nothing shows it rising. A
        // conservative choice: no climax is suggested there.
        if (prominence < kMinProminence) continue;
        // The climax's stretch: from the detail's top near the arc's peak (the
        // arc fuses close bursts and may peak in the dip between them), where
        // the detail stays above half the prominence, within the action.
        const auto from = out.detail.begin() + std::ptrdiff_t(std::max(intro, p - std::min(p, half_arc)));
        const auto to = out.detail.begin() + std::ptrdiff_t(std::min(n, p + half_arc + 1));
        const size_t q = size_t(std::max_element(from, to) - out.detail.begin());
        const double level = out.arc[p] - 0.5 * prominence;
        size_t a = q, b = q + 1;
        while (a > intro && out.detail[a - 1] >= level) --a;
        while (b < n && out.detail[b] >= level) ++b;
        // A climax is the exception: the rest of the action is longer than it.
        if (2 * (b - a) >= n - intro) continue;
        // Its strength: per input, how many times its own action level it
        // reaches (so a climax in the sound alone counts like one in the
        // picture alone), weighted like the intensity.
        const Top top = TopOf(out.detail, a, b, gap, noise);
        double over_rest = 0, over_flank = 0;
        if (!motion.empty()) {
            const Rise r = RiseOf(motion_detail, motion_live, motion_action, intro, a, b, top.first, top.last, flank);
            over_rest += wm * r.over_rest;
            over_flank += wm * r.over_flank;
        }
        if (!loud.empty()) {
            const Rise r = RiseOf(loud_detail, loud_live, loud_action, intro, a, b, top.first, top.last, flank);
            over_rest += wl * r.over_rest;
            over_flank += wl * r.over_flank;
        }
        over_rest /= wm + wl;
        over_flank /= wm + wl;
        // It stands min_gain over the rest of the action and kFlankGain over
        // the action beside it, or min_gain there too behind a sudden edge.
        const double top_level = MeanOf(out.detail, top.first, top.last);
        const double left = FlankMedian(out.detail, live, std::max(intro, a - std::min(a, flank)), a, flank / 4);
        const double right = FlankMedian(out.detail, live, b, std::min(n, b + flank), flank / 4);
        // A clip without an intro that opens at its top: no clear rise from
        // the action before the peak (its left flank, by half its prominence
        // and clear of the noise), and the arc never falls halfway down to
        // the action beside it before the clip's start; also a wiggle on a
        // loud opening.
        if (intro == 0) {
            const double rise = std::max({kMinProminence, 2 * noise, 0.5 * prominence});
            if (left < 0 || out.arc[p] - left < rise) {
                const double low = left < 0 ? right : right < 0 ? left : std::min(left, right);
                const double band = low >= 0 ? std::max(rise, 0.5 * (out.arc[p] - low)) : rise;
                if (OpensAtTop(out.arc, p, band)) continue;
            }
        }
        const auto above_noise = [&](double base) { return top_level - base >= kSuddenNoise * noise; };
        const bool sudden_left = left >= 0 && above_noise(left) &&
                                 SuddenEdge(out.detail, top.first, -1, intro, n, left, top_level, sudden_steps);
        const bool sudden = sudden_left || (right >= 0 && above_noise(right) &&
                                            SuddenEdge(out.detail, top.last - 1, +1, intro, n, right, top_level, sudden_steps));
        // The last step of a staircase: a sudden rise held to the clip's end.
        const double flank_gain = sudden_left && b == n ? kLastStepGain : sudden ? o.min_gain : kFlankGain;
        if (over_rest < o.min_gain || over_flank < flank_gain) continue;
        const double strength = over_rest - 1;
        ArcPeak peak;
        peak.t_ms = std::llround(top.centre * double(s.step_ms));
        peak.start_ms = int64_t(top.first) * s.step_ms;
        peak.end_ms = int64_t(top.last) * s.step_ms;
        peak.prominence = prominence;
        peak.confidence = std::clamp(strength / kFullStrength, 0.0, 1.0);
        if (!motion.empty() && !loud.empty()) {
            const double apart = std::abs(TopOf(motion_detail, a, b, gap, 0).centre - TopOf(loud_detail, a, b, gap, 0).centre);
            if (apart * double(s.step_ms) > double(kAgreementMs)) peak.confidence *= kDisagreement;
        }
        const double lateness = kEarlyWeight + (1 - kEarlyWeight) * double(peak.t_ms) / duration;
        candidates.push_back({peak, strength, strength * lateness, top_level, a, b});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& x, const Candidate& y) { return x.score > y.score; });

    // More than one climax only in a clip that is calm outside its climaxes:
    // the spread (p90 - p10) of the action outside every candidate's stretch
    // is small against the primary's rise over that action's median. A busy
    // clip (drift, a restless scene) gets one climax, as with the old rules.
    bool calm = false;
    if (!candidates.empty()) {
        std::vector<bool> inside(n, false);
        for (const auto& c : candidates) std::fill(inside.begin() + std::ptrdiff_t(c.a), inside.begin() + std::ptrdiff_t(c.b), true);
        std::vector<double> outside;
        for (size_t i = intro; i < n; ++i)
            if (!inside[i] && live[i]) outside.push_back(out.detail[i]);
        if (!outside.empty()) {
            const double spread = Percentile(outside, 0.9) - Percentile(outside, 0.1);
            calm = spread <= kCalmSpread * (candidates.front().top_level - Percentile(outside, 0.5));
        }
    }

    // The primary, then the next ones that are high enough and far enough from every one taken.
    for (const auto& c : candidates) {
        if (!out.peaks.empty()) {
            if (!calm) break;
            if (int(out.peaks.size()) > o.max_secondary) break;
            if (c.strength < o.secondary_share * candidates.front().strength) continue;
            const bool apart = std::all_of(out.peaks.begin(), out.peaks.end(), [&](const ArcPeak& q) {
                return std::llabs(q.t_ms - c.peak.t_ms) >= gap_ms;
            });
            if (!apart) continue;
        }
        out.peaks.push_back(c.peak);
    }
    return out;
}

}  // namespace samn
