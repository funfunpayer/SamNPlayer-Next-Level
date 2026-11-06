// Phase suggestions on synthetic arcs with known truth: the rules before the
// intensity arc (LegacyRules, their code kept here unchanged) against
// SuggestPhases now (D-165). Deterministic (seeded LCG); prints a Markdown
// table. Built with the tests, not run by ctest: by hand, the CPU times
// depend on the machine and the build type.
//   phase_bench [--variants N]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>

#include "samn/phase_rules.h"

namespace {

using samn::LabelPhase;
using samn::LabelProject;
using samn::PhaseRuleOptions;
using samn::PhaseSignals;

// The rules as they were before D-165 (core/src/phase_rules.cpp at 33a5723).
namespace legacy {

double Percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(std::floor(q * double(v.size() - 1) + 0.5)));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

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

std::vector<LabelPhase> LegacyRules(const PhaseSignals& s, const PhaseRuleOptions& o = {}) {
    const size_t n = std::min(s.luma.size(), s.motion.size());
    if (n == 0 || s.step_ms <= 0) return {};
    const auto steps = [&](int64_t ms) { return std::max<size_t>(1, size_t((ms + s.step_ms - 1) / s.step_ms)); };
    const auto at = [&](size_t i) { return int64_t(i) * s.step_ms; };
    const bool has_sound = s.loudness.size() >= n;
    const bool has_tempo = s.tempo.size() >= n;

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

    LabelProject p;
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

}  // namespace legacy

// Numerical Recipes' LCG: the same clips on every run and platform.
struct Lcg {
    uint32_t state;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24);
    }
    double In(double lo, double hi) { return lo + (hi - lo) * Next(); }
};

enum class Kind { kEarly, kLate, kDouble, kBuildUp, kTalky, kFlatMusic, kNoise };
const char* Name(Kind k) {
    switch (k) {
        case Kind::kEarly: return "early climax (20-35 %)";
        case Kind::kLate: return "late climax (75-88 %)";
        case Kind::kDouble: return "two climaxes";
        case Kind::kBuildUp: return "linear build-up";
        case Kind::kTalky: return "loud talky intro";
        case Kind::kFlatMusic: return "flat music, motion peak";
        case Kind::kNoise: return "noise, no arc";
    }
    return "";
}

struct Clip {
    PhaseSignals s;
    std::vector<double> climaxes;  // truth, seconds
    double intro_s = -1;           // truth, -1: none
};

// 150-600 s at 500 ms per step, +-15 % jitter on every sample (+-50 % for noise).
Clip Make(Kind kind, Lcg& r) {
    Clip c;
    c.s.step_ms = 500;
    const double length = r.In(150, 600), intro = r.In(8, 25);
    const double base_m = r.In(.15, .25), base_l = r.In(.25, .4);
    const double peak_m = base_m * r.In(1.8, 2.2), peak_l = r.In(.8, .95), half = r.In(5, 10);
    const double music = r.In(.5, .7);
    double c1 = -1, c2 = -1, ramp_end = -1;
    switch (kind) {
        case Kind::kEarly: c1 = length * r.In(.2, .35); break;
        case Kind::kDouble: c1 = length * r.In(.35, .5); c2 = length * r.In(.78, .88); break;
        case Kind::kBuildUp: ramp_end = length * r.In(.75, .88); break;
        case Kind::kNoise: break;
        default: c2 = length * r.In(.75, .88); break;
    }
    const double tail = ramp_end > 0 ? ramp_end : c2 > 0 ? c2 + half : length + 1;  // calm after the last climax
    if (kind != Kind::kNoise) c.intro_s = intro;
    if (c1 > 0) c.climaxes.push_back(c1);
    if (c2 > 0) c.climaxes.push_back(c2);
    if (ramp_end > 0) c.climaxes.push_back(ramp_end - 5);  // the top 10 s of the rise
    const double jitter = kind == Kind::kNoise ? 1.0 : 0.3;
    for (double t = 0; t < length; t += 0.5) {
        double m = base_m, l = base_l;
        if (kind == Kind::kNoise) {
            // nothing but the base level
        } else if (t < intro) {
            m = 0.1 * base_m;
            l = kind == Kind::kTalky ? r.In(.7, .9) : 0.6 * base_l;
        } else if (t >= tail) {
            m = 0.25 * base_m;
            l = 0.3 * base_l;
        } else if (ramp_end > 0) {
            const double u = (t - intro) / (ramp_end - intro);
            m = base_m * 0.5 + u * (peak_m - base_m * 0.5);
            l = base_l + u * (peak_l - base_l);
        } else if ((c1 > 0 && std::abs(t - c1) < half) || (c2 > 0 && std::abs(t - c2) < half)) {
            m = peak_m;
            l = peak_l;
        }
        if (kind == Kind::kFlatMusic) l = music;
        c.s.luma.push_back(0.5);
        c.s.motion.push_back(std::max(0.0, m * (1 + jitter * (r.Next() - 0.5))));
        c.s.loudness.push_back(std::clamp(l * (1 + jitter * (r.Next() - 0.5)), 0.0, 1.0));
    }
    return c;
}

constexpr double kClimaxHitS = 5;  // the acceptance of D-165
constexpr double kIntroHitS = 3;
constexpr int kTimingRuns = 20;

struct Score {
    int climaxes = 0, climax_hits = 0, false_climaxes = 0, clips = 0, intro_hits = 0;
    std::vector<double> climax_err, intro_err;
    double cpu_us = 0;
};

void Grade(const Clip& c, const std::vector<LabelPhase>& phases, Score& s) {
    std::vector<double> mids;
    double intro = -1;
    for (const auto& p : phases) {
        if (p.name == "climax") mids.push_back((p.start_ms + p.end_ms) / 2000.0);
        if (p.name == "intro") intro = p.end_ms / 1000.0;
    }
    for (double truth : c.climaxes) {
        ++s.climaxes;
        double best = 1e9;
        for (double m : mids) best = std::min(best, std::abs(m - truth));
        if (best < 1e9) s.climax_err.push_back(best);
        if (best <= kClimaxHitS) ++s.climax_hits;
    }
    for (double m : mids)
        if (std::none_of(c.climaxes.begin(), c.climaxes.end(), [&](double t) { return std::abs(m - t) <= kClimaxHitS; }))
            ++s.false_climaxes;
    ++s.clips;
    if (c.intro_s < 0) {
        if (intro < 0) ++s.intro_hits;
    } else if (intro >= 0) {
        s.intro_err.push_back(std::abs(intro - c.intro_s));
        if (std::abs(intro - c.intro_s) <= kIntroHitS) ++s.intro_hits;
    }
}

template <typename Rules>
void Run(const Clip& c, Rules rules, Score& s) {
    Grade(c, rules(c.s), s);
    const std::clock_t t0 = std::clock();
    volatile size_t sink = 0;  // the calls are not optimised away
    for (int i = 0; i < kTimingRuns; ++i) sink = sink + rules(c.s).size();
    const std::clock_t t1 = std::clock();
    s.cpu_us += 1e6 * double(t1 - t0) / CLOCKS_PER_SEC / kTimingRuns;
}

std::string Median(std::vector<double> v) {
    if (v.empty()) return "-";
    std::sort(v.begin(), v.end());
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v[v.size() / 2]);
    return buf;
}

void Row(const char* scenario, const char* rules, const Score& s) {
    std::printf("| %s | %s | %d/%d | %s | %d | %d/%d | %s | %.0f |\n", scenario, rules, s.climax_hits, s.climaxes,
                Median(s.climax_err).c_str(), s.false_climaxes, s.intro_hits, s.clips, Median(s.intro_err).c_str(),
                s.cpu_us / std::max(1, s.clips));
}

void Add(Score& total, const Score& s) {
    total.climaxes += s.climaxes;
    total.climax_hits += s.climax_hits;
    total.false_climaxes += s.false_climaxes;
    total.clips += s.clips;
    total.intro_hits += s.intro_hits;
    total.climax_err.insert(total.climax_err.end(), s.climax_err.begin(), s.climax_err.end());
    total.intro_err.insert(total.intro_err.end(), s.intro_err.begin(), s.intro_err.end());
    total.cpu_us += s.cpu_us;
}

}  // namespace

int main(int argc, char** argv) {
    int variants = 25;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--variants") == 0) variants = std::max(1, std::atoi(argv[i + 1]));
    const auto now = [](const PhaseSignals& s) { return samn::SuggestPhases(s); };
    const auto before = [](const PhaseSignals& s) { return legacy::LegacyRules(s); };

    std::printf("%d clips per scenario, climax hit within %.0f s of the truth (midpoint of a suggested climax), "
                "intro hit within %.0f s (noise: no intro suggested)\n\n",
                variants, kClimaxHitS, kIntroHitS);
    std::printf("| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | "
                "Median intro error (s) | CPU us / call |\n|---|---|---|---|---|---|---|---|\n");
    Score total_before, total_now;
    const Kind kinds[] = {Kind::kEarly, Kind::kLate, Kind::kDouble, Kind::kBuildUp,
                          Kind::kTalky, Kind::kFlatMusic, Kind::kNoise};
    for (Kind kind : kinds) {
        Lcg r{uint32_t(0x5eed + int(kind) * 7919)};
        Score b, a;
        for (int v = 0; v < variants; ++v) {
            const Clip c = Make(kind, r);
            Run(c, before, b);
            Run(c, now, a);
        }
        Row(Name(kind), "before", b);
        Row(Name(kind), "arc", a);
        Add(total_before, b);
        Add(total_now, a);
    }
    Row("**all**", "before", total_before);
    Row("**all**", "arc", total_now);
    return 0;
}
