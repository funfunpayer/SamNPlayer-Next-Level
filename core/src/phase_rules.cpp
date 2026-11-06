#include "samn/phase_rules.h"

#include <algorithm>

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
    const auto steps = [&](int64_t ms) { return std::max<size_t>(1, size_t((ms + s.step_ms - 1) / s.step_ms)); };
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

    PhaseSignals clip;  // the arc sees the stretch the picture covers
    clip.step_ms = s.step_ms;
    clip.motion.assign(s.motion.begin(), s.motion.begin() + std::ptrdiff_t(n));
    if (has_sound) clip.loudness.assign(s.loudness.begin(), s.loudness.begin() + std::ptrdiff_t(n));
    ArcOptions ao;
    ao.steady_ms = o.steady_ms;
    ao.min_gain = o.climax_gain;
    const IntensityArc arc = ComputeArc(clip, ao);

    // Intro: from the first picture until the action begins.
    size_t first = 0;
    while (first < n && in_empty[first]) ++first;
    size_t action = std::max(first, index(arc.intro_end_ms));
    if (action > first && at(action) - at(first) >= o.min_intro_ms)
        p.SetPhase(at(first), at(action), "intro", false, "rules");
    else
        action = first;
    size_t last = n;
    while (last > action && in_empty[last - 1]) --last;

    // Climaxes: the arc's peaks, each clamped to a sensible length around its top.
    size_t latest_a = 0, latest_b = 0;
    for (const auto& peak : arc.peaks) {
        const size_t top = index(peak.t_ms);
        if (top < action || top >= last || in_empty[top]) continue;
        size_t a = std::max(action, index(peak.start_ms)), b = std::clamp(index(peak.end_ms), top + 1, last);
        const size_t lo = steps(o.climax_min_ms), hi = steps(o.climax_max_ms);
        while (b - a < lo && (a > action || b < last)) {
            if (a > action) --a;
            if (b - a < lo && b < last) ++b;
        }
        while (b - a > hi) {
            if (top - a > b - top) ++a;
            else --b;
        }
        p.SetPhase(at(a), at(b), "climax", false, "rules");
        if (b > latest_b) {
            latest_a = a;
            latest_b = b;
        }
    }

    // Cool-down: after the last climax until the next empty stretch, when it is
    // calmer than the climax and than the action before it (after an early
    // climax the action often just goes on).
    if (latest_b > 0) {
        size_t c = latest_b;
        while (c < last && !in_empty[c]) ++c;
        const double after = Mean(arc.detail, latest_b, c), during = Mean(arc.detail, latest_a, latest_b);
        const bool calmer_than_before = latest_a == action || after < kCalmer * Mean(arc.detail, action, latest_a);
        if (at(c) - at(latest_b) >= o.min_cooldown_ms && after < kCalmer * during && calmer_than_before)
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
