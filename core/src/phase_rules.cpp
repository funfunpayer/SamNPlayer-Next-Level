#include "samn/phase_rules.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "samn/intensity_arc.h"

namespace samn {

namespace {

// A cool-down is clearly calmer than what came before it.
constexpr double kCalmer = 0.8;

// The mean of v[a, b).
double Mean(const std::vector<double>& v, size_t a, size_t b) {
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += v[i];
    return b > a ? sum / double(b - a) : 0;
}

}  // namespace

std::vector<LabelPhase> SuggestPhases(const PhaseSignals& s, const PhaseRuleOptions& o) {
    const size_t n = std::min(s.luma.size(), s.motion.size());
    if (n == 0 || s.step_ms <= 0) return {};
    const auto steps = [&](int64_t ms) {
        const int64_t m = std::clamp<int64_t>(ms, 0, std::numeric_limits<int64_t>::max() - s.step_ms);
        return std::max<size_t>(1, size_t((m + s.step_ms - 1) / s.step_ms));
    };
    const auto at = [&](size_t i) { return int64_t(i) * s.step_ms; };
    const auto index = [&](int64_t ms) { return std::min(n, size_t(std::max<int64_t>(0, ms) / s.step_ms)); };
    const bool has_sound = s.loudness.size() >= n;

    // Empty pictures: dark, or still and silent.
    std::vector<bool> in_empty(n, false);
    std::vector<std::pair<size_t, size_t>> empty_runs;
    const auto empty = [&](size_t i) {
        return s.luma[i] < o.dark_luma || (s.motion[i] < o.still_motion && (!has_sound || s.loudness[i] < 0.05));
    };
    for (size_t i = 0; i < n;) {
        if (!empty(i)) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && empty(j)) ++j;
        if (j - i >= steps(o.min_empty_ms)) {
            empty_runs.push_back({i, j});
            std::fill(in_empty.begin() + std::ptrdiff_t(i), in_empty.begin() + std::ptrdiff_t(j), true);
        }
        i = j;
    }
    LabelProject p;  // SetPhase keeps the stretches apart and sorted
    const auto add_empty = [&] {
        for (const auto& [a, b] : empty_runs) p.SetPhase(at(a), at(b), "empty", false, "rules");
        return p.phases;
    };
    if (std::all_of(in_empty.begin(), in_empty.end(), [](bool e) { return e; })) return add_empty();

    // The arc sees the pictures from the first to the last one that is not
    // empty; its times are from `first` on. Empty stretches between have no
    // motion and no sound, and the arc leaves such samples out of its
    // statistics.
    size_t first = 0, last = n;
    while (first < n && in_empty[first]) ++first;
    while (last > first && in_empty[last - 1]) --last;
    PhaseSignals clip;
    clip.step_ms = s.step_ms;
    clip.motion.assign(s.motion.begin() + std::ptrdiff_t(first), s.motion.begin() + std::ptrdiff_t(last));
    if (has_sound) clip.loudness.assign(s.loudness.begin() + std::ptrdiff_t(first), s.loudness.begin() + std::ptrdiff_t(last));
    ArcOptions ao;
    ao.steady_ms = o.steady_ms;
    ao.min_gain = o.climax_gain;
    const IntensityArc arc = ComputeArc(clip, ao);
    const int64_t shift = at(first);
    const auto detail_mean = [&](size_t a, size_t b) { return Mean(arc.detail, a - first, b - first); };

    // Intro: from the first picture until the action begins.
    size_t action = std::max(first, index(shift + arc.intro_end_ms));
    // A short clip has a short intro: the minimum scales like the arc.
    const int64_t min_intro_ms = std::llround(double(o.min_intro_ms) * ArcScale(at(last) - shift));
    if (action > first && at(action) - at(first) >= min_intro_ms)
        p.SetPhase(at(first), at(action), "intro", false, "rules");
    else
        action = first;

    // Climaxes: the arc's peaks in rank order, each one stretch of free
    // pictures (no empty stretch, a step away from every other climax, so
    // that none is cut into pieces or merged past climax_max_ms), clamped to a
    // sensible length around its top.
    std::vector<bool> taken = in_empty;
    // False outside [action, last), also for a - 1 at 0 (it wraps to the largest size_t).
    const auto free = [&](size_t i) { return i >= action && i < last && !taken[i]; };
    size_t latest_a = 0, latest_b = 0;
    for (const auto& peak : arc.peaks) {
        const size_t from = std::max(action, index(shift + peak.start_ms)), to = std::min(last, index(shift + peak.end_ms) + 1);
        if (from >= to) continue;
        // The longest free piece of its stretch (an empty stretch may cut it);
        // its top is the peak's own when that is free, else the piece's middle.
        size_t a = from, b = from;
        for (size_t i = from; i < to;) {
            if (!free(i)) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < to && free(j)) ++j;
            if (j - i > b - a) {
                a = i;
                b = j;
            }
            i = j;
        }
        if (a == b) continue;
        const size_t peak_top = index(shift + peak.t_ms);
        const size_t top = peak_top >= a && peak_top < b ? peak_top : a + (b - a) / 2;
        const size_t lo = steps(o.climax_min_ms), hi = steps(o.climax_max_ms);
        while (b - a < lo && (free(a - 1) || free(b))) {
            if (free(a - 1)) --a;
            if (b - a < lo && free(b)) ++b;
        }
        while (b - a > hi) {
            if (top - a > b - top) ++a;
            else --b;
        }
        p.SetPhase(at(a), at(b), "climax", false, "rules");
        const size_t keep_from = a > 0 ? a - 1 : 0, keep_to = std::min(n, b + 1);
        std::fill(taken.begin() + std::ptrdiff_t(keep_from), taken.begin() + std::ptrdiff_t(keep_to), true);
        if (b > latest_b) {
            latest_a = a;
            latest_b = b;
        }
    }

    // Cool-down: after the last climax until the next empty stretch, when it is
    // calmer than the climax and ends calmer than the action before it (after
    // an early climax the action often just goes on; a fade from the
    // climax's own level ends calm).
    if (latest_b > 0) {
        size_t c = latest_b;
        while (c < last && !in_empty[c]) ++c;
        const double after = detail_mean(latest_b, c), during = detail_mean(latest_a, latest_b);
        const double end = detail_mean(c - (c - latest_b) / 3, c);
        const bool ends_calmer = latest_a == action || end < kCalmer * detail_mean(action, latest_a);
        if (at(c) - at(latest_b) >= o.min_cooldown_ms && after < kCalmer * during && ends_calmer)
            p.SetPhase(at(latest_b), at(c), "cool-down", false, "rules");
    }

    return add_empty();
}

int ApplyPhaseSuggestions(LabelProject& p, const std::vector<LabelPhase>& suggestions) {
    const std::vector<LabelPhase> taken = p.phases;
    int added = 0;
    for (const auto& s : suggestions) {
        bool any = false;
        int64_t from = s.start_ms;
        for (const auto& t : taken) {
            if (t.end_ms <= from || t.start_ms >= s.end_ms) continue;
            if (t.start_ms > from) {
                p.SetPhase(from, t.start_ms, s.name, false, s.source);
                any = true;
            }
            from = std::max(from, t.end_ms);
        }
        if (from < s.end_ms) {
            p.SetPhase(from, s.end_ms, s.name, false, s.source);
            any = true;
        }
        if (any) ++added;
    }
    return added;
}

VideoPhaseStep StepVideoPhases(const std::vector<LabelPhase>& phases, int64_t ms, int last_climax) {
    VideoPhaseStep step;
    const auto skippable = [](const LabelPhase& p) { return p.confirmed && (p.name == "intro" || p.name == "empty"); };
    int64_t at = ms;
    for (bool moved = true; moved;) {  // touching stretches chain
        moved = false;
        for (const auto& p : phases)
            if (skippable(p) && at >= p.start_ms && at < p.end_ms) {
                at = p.end_ms;
                moved = true;
            }
    }
    if (at != ms) step.seek_to = at;
    for (size_t i = 0; i < phases.size(); ++i) {
        const auto& p = phases[i];
        if (p.confirmed && p.name == "climax" && at >= p.start_ms && at < p.end_ms && int(i) != last_climax)
            step.climax = int(i);
    }
    return step;
}

}  // namespace samn
