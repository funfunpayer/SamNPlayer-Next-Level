#include "samn/phase_rules.h"

#include <algorithm>
#include <cmath>

namespace samn {

namespace {

double Percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(std::floor(q * double(v.size() - 1) + 0.5)));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

// A centred moving average over `window` steps.
std::vector<double> Smooth(const std::vector<double>& v, size_t window) {
    if (v.empty() || window <= 1) return v;
    std::vector<double> out(v.size());
    const size_t half = window / 2;
    for (size_t i = 0; i < v.size(); ++i) {
        const size_t a = i >= half ? i - half : 0, b = std::min(v.size(), i + half + 1);
        double sum = 0;
        for (size_t j = a; j < b; ++j) sum += v[j];
        out[i] = sum / double(b - a);
    }
    return out;
}

LabelPhase Suggestion(int64_t a, int64_t b, const char* name) { return {a, b, name, false, "rules"}; }

}  // namespace

std::vector<LabelPhase> SuggestPhases(const PhaseSignals& s, const PhaseRuleOptions& o) {
    const size_t n = std::min(s.luma.size(), s.motion.size());
    if (n == 0 || s.step_ms <= 0) return {};
    const auto steps = [&](int64_t ms) { return std::max<size_t>(1, size_t((ms + s.step_ms - 1) / s.step_ms)); };
    const auto at = [&](size_t i) { return int64_t(i) * s.step_ms; };
    const bool has_sound = s.loudness.size() >= n;
    const bool has_tempo = s.tempo.size() >= n;

    // Empty pictures: dark, or still and silent.
    std::vector<bool> empty(n);
    for (size_t i = 0; i < n; ++i)
        empty[i] = s.luma[i] < o.dark_luma ||
                   (s.motion[i] < o.still_motion && (!has_sound || s.loudness[i] < 0.05));
    std::vector<std::pair<size_t, size_t>> empty_runs;
    for (size_t i = 0; i < n;) {
        if (!empty[i]) { ++i; continue; }
        size_t j = i;
        while (j < n && empty[j]) ++j;
        if (j - i >= steps(o.min_empty_ms)) empty_runs.push_back({i, j});
        i = j;
    }
    std::vector<bool> in_empty(n, false);
    for (const auto& [a, b] : empty_runs)
        for (size_t i = a; i < b; ++i) in_empty[i] = true;

    // Activity: the motion against the clip's own busy level, with the
    // script's tempo when there is one.
    std::vector<double> active_motion;
    for (size_t i = 0; i < n; ++i)
        if (!in_empty[i]) active_motion.push_back(s.motion[i]);
    const double motion_top = std::max(1e-6, Percentile(active_motion, 0.9));
    std::vector<double> activity(n);
    for (size_t i = 0; i < n; ++i) {
        const double m = std::min(1.0, s.motion[i] / motion_top);
        activity[i] = has_tempo ? 0.5 * m + 0.5 * std::clamp(s.tempo[i], 0.0, 1.0) : m;
    }
    activity = Smooth(activity, steps(4000));

    std::vector<double> busy_values;
    for (size_t i = 0; i < n; ++i)
        if (!in_empty[i]) busy_values.push_back(activity[i]);
    if (busy_values.empty()) {
        std::vector<LabelPhase> out;
        for (const auto& [a, b] : empty_runs) out.push_back(Suggestion(at(a), at(b), "empty"));
        return out;
    }
    const double threshold = o.active_share * Percentile(busy_values, 0.9);

    LabelProject p;  // SetPhase keeps the stretches apart and sorted
    // Intro: from the first picture until the action stays steady.
    size_t first = 0;
    while (first < n && in_empty[first]) ++first;
    size_t action = n, run = 0;
    for (size_t i = first; i < n; ++i) {
        run = (!in_empty[i] && activity[i] >= threshold) ? run + 1 : 0;
        if (run >= steps(o.steady_ms)) {
            action = i + 1 - run;
            break;
        }
    }
    if (action < n && action > first && at(action) - at(first) >= o.min_intro_ms)
        p.SetPhase(at(first), at(action), "intro", false, "rules");
    if (action >= n) action = first;

    // Climax: where action and sound peak together in the second half.
    std::vector<double> intensity = Smooth(activity, steps(10000));
    if (has_sound) {
        const auto loud = Smooth(std::vector<double>(s.loudness.begin(), s.loudness.begin() + std::ptrdiff_t(n)), steps(10000));
        for (size_t i = 0; i < n; ++i) intensity[i] = 0.6 * intensity[i] + 0.4 * std::clamp(loud[i], 0.0, 1.0);
    }
    size_t last = n;
    while (last > action && in_empty[last - 1]) --last;
    std::vector<double> active_intensity;
    for (size_t i = action; i < last; ++i)
        if (!in_empty[i]) active_intensity.push_back(intensity[i]);
    const size_t search_from = std::max(action, action + (last - action) / 2);
    size_t peak = n;
    for (size_t i = search_from; i < last; ++i)
        if (!in_empty[i] && (peak == n || intensity[i] > intensity[peak])) peak = i;
    const double median = Percentile(active_intensity, 0.5);
    if (peak < n && intensity[peak] > 0 && intensity[peak] >= o.climax_gain * median) {
        const double floor = 0.85 * intensity[peak];
        size_t a = peak, b = peak + 1;
        while (a > action && !in_empty[a - 1] && intensity[a - 1] >= floor) --a;
        while (b < last && !in_empty[b] && intensity[b] >= floor) ++b;
        // Clamp to a sensible length around the peak.
        const size_t lo = steps(o.climax_min_ms), hi = steps(o.climax_max_ms);
        while (b - a < lo && (a > action || b < last)) {
            if (a > action) --a;
            if (b - a < lo && b < last) ++b;
        }
        while (b - a > hi) {
            if (peak - a > b - peak) ++a;
            else --b;
        }
        p.SetPhase(at(a), at(b), "climax", false, "rules");

        // Cool-down: what follows until the next empty stretch, when it is calmer.
        size_t c = b;
        while (c < last && !in_empty[c]) ++c;
        if (at(c) - at(b) >= o.min_cooldown_ms) {
            double after = 0, during = 0;
            for (size_t i = b; i < c; ++i) after += activity[i];
            for (size_t i = a; i < b; ++i) during += activity[i];
            after /= double(c - b);
            during /= double(b - a);
            if (after < 0.8 * during) p.SetPhase(at(b), at(c), "cool-down", false, "rules");
        }
    }

    for (const auto& [a, b] : empty_runs) p.SetPhase(at(a), at(b), "empty", false, "rules");
    return p.phases;
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
