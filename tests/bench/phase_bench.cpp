// Phase suggestions on synthetic arcs with known truth: the rules before the
// intensity arc (LegacyRules, the regression oracle) against SuggestPhases
// now (D-165). Two parts, both deterministic:
//  - 24 hand-made scenarios (seeded LCG), with CPU times;
//  - families of random arcs (thousands of piecewise-linear arcs with one
//    known strongest peak, or with none), drawn from a TRAIN and a HELD-OUT
//    set of seeds. Constants were chosen on the train set only.
// Exit 1 when, on any scenario or family (train or held-out), the arc hits
// fewer climaxes than the old rules, more than 2 % fewer intros, or suggests
// more false climaxes per clip than they do plus kFalseTolerance (plus
// kUnusableTolerance where the old rules themselves exceed kUnusableRate:
// there both rule sets are unreliable). Built with the tests, not run by ctest:
// by hand, the CPU times depend on the machine and the build type.
//   phase_bench [--variants N] [--family-clips N] [--split train|held-out|both] [--strict-hits]
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

// The rules as they were before D-165 (core/src/phase_rules.cpp at 33a5723),
// less two inputs that no caller ever set: the script tempo (never filled)
// and PhaseRuleOptions::active_share (always its default, now a constant).
namespace legacy {

constexpr double kActiveShare = 0.35;

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
    for (size_t i = 0; i < n; ++i) activity[i] = std::min(1.0, s.motion[i] / motion_top);
    activity = Smooth(activity, steps(4000));

    std::vector<double> busy_values;
    for (size_t i = 0; i < n; ++i)
        if (!in_empty[i]) busy_values.push_back(activity[i]);
    if (busy_values.empty()) {
        std::vector<LabelPhase> out;
        for (const auto& [a, b] : empty_runs) out.push_back(Suggestion(at(a), at(b), "empty"));
        return out;
    }
    const double threshold = kActiveShare * Percentile(busy_values, 0.9);

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

// --strict-hits: a family's climax is a hit only within 5 s of the top's
// centre, as in the scenarios (by default anywhere on a wider flat top).
bool g_strict_hits = false;

// Numerical Recipes' LCG: the same clips on every run and platform.
struct Lcg {
    uint32_t state;
    double Next() {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24);
    }
    double In(double lo, double hi) { return lo + (hi - lo) * Next(); }
};

enum class Kind {
    kEarly, kLate, kDouble, kBuildUp, kTalky, kFlatMusic, kNoise,  // the first set
    kAtEnd, kAtStart, kPlateau, kShort30, kShort45, kShort60, kShort90,
    kBursts10, kBursts15, kBursts20, kBursts25, kBlackCut, kDrift, kConcert, kMidAction,
    kSoundOnly, kMotionOnly, kCount
};
const char* Name(Kind k) {
    switch (k) {
        case Kind::kEarly: return "early climax (20-35 %)";
        case Kind::kLate: return "late climax (75-88 %)";
        case Kind::kDouble: return "two climaxes";
        case Kind::kBuildUp: return "linear build-up";
        case Kind::kTalky: return "loud talky intro";
        case Kind::kFlatMusic: return "flat music, motion peak";
        case Kind::kNoise: return "noise, no arc";
        case Kind::kAtEnd: return "climax runs to the end";
        case Kind::kAtStart: return "opens at its top (no climax)";
        case Kind::kPlateau: return "plateau of 60-120 s";
        case Kind::kShort30: return "30 s clip";
        case Kind::kShort45: return "45 s clip";
        case Kind::kShort60: return "60 s clip";
        case Kind::kShort90: return "90 s clip";
        case Kind::kBursts10: return "two bursts 10 s apart";
        case Kind::kBursts15: return "two bursts 15 s apart";
        case Kind::kBursts20: return "two bursts 20 s apart";
        case Kind::kBursts25: return "two bursts 25 s apart";
        case Kind::kBlackCut: return "black cut inside the climax";
        case Kind::kDrift: return "slow drift, no climax";
        case Kind::kConcert: return "loud concert opening";
        case Kind::kMidAction: return "starts mid-action";
        case Kind::kSoundOnly: return "climax in the sound only";
        case Kind::kMotionOnly: return "climax in the motion only, no sound";
        case Kind::kCount: break;
    }
    return "";
}

struct Clip {
    PhaseSignals s;
    std::vector<std::vector<double>> climaxes;  // truth, seconds; any time of a set counts
    double top_half_s = 0;                      // a hit may lie this far from the truth (a wide top), at least 5 s
    double intro_s = -1;                        // truth, -1: none
};

// Each clip: 500 ms per step and +-15 % jitter on every sample (+-50 % for
// noise). Unless the scenario says otherwise: 150-600 s, a calm intro of
// 8-25 s, action at a base motion of 0.15-0.25 and loudness 0.25-0.4,
// climaxes of 10-20 s at 1.8-2.2 x the motion and loudness 0.8-0.95, and a
// calm tail after the last one.
Clip Make(Kind kind, Lcg& r) {
    Clip c;
    c.s.step_ms = 500;
    double length = r.In(150, 600), intro = r.In(8, 25);
    const double base_m = r.In(.15, .25), base_l = r.In(.25, .4);
    const double peak_m = base_m * r.In(1.8, 2.2), peak_l = r.In(.8, .95);
    double half = r.In(5, 10);
    const double music = r.In(.5, .7);
    std::vector<double> centres;  // climaxes of +-half
    double ramp_end = -1, tail = -1, plateau_half = 0, opening = 0, black_from = -1, black_to = -1;
    bool talky = false, flat_music = false, noise = false, drift = false, sound_only = false, no_sound = false;
    const auto short_clip = [&](double seconds) {
        length = seconds;
        intro = seconds * r.In(.1, .2);
        half = std::min(half, seconds / 10);
        centres = {seconds * r.In(.55, .75)};
    };
    const auto bursts = [&](double apart) {
        half = r.In(3, 4);
        const double first = length * r.In(.6, .75);
        centres = {first, first + apart};
    };
    switch (kind) {
        case Kind::kEarly: centres = {length * r.In(.2, .35)}; break;
        case Kind::kLate: centres = {length * r.In(.75, .88)}; break;
        case Kind::kDouble: centres = {length * r.In(.35, .5), length * r.In(.78, .88)}; break;
        case Kind::kBuildUp: ramp_end = length * r.In(.75, .88); break;
        case Kind::kTalky: talky = true; centres = {length * r.In(.75, .88)}; break;
        case Kind::kFlatMusic: flat_music = true; centres = {length * r.In(.75, .88)}; break;
        case Kind::kNoise: noise = true; intro = 0; break;
        case Kind::kAtEnd: centres = {length - half}; break;
        case Kind::kAtStart: intro = 0; centres = {half}; break;
        case Kind::kPlateau: plateau_half = r.In(30, 60); centres = {length * r.In(.4, .6)}; break;
        case Kind::kShort30: short_clip(30); break;
        case Kind::kShort45: short_clip(45); break;
        case Kind::kShort60: short_clip(60); break;
        case Kind::kShort90: short_clip(90); break;
        case Kind::kBursts10: bursts(10); break;
        case Kind::kBursts15: bursts(15); break;
        case Kind::kBursts20: bursts(20); break;
        case Kind::kBursts25: bursts(25); break;
        case Kind::kBlackCut:
            half = r.In(7, 10);
            centres = {length * r.In(.75, .85)};
            black_from = centres[0] + r.In(-half + 1, half - 4);
            black_to = black_from + r.In(2, 3);
            break;
        case Kind::kDrift: drift = true; intro = 0; break;
        case Kind::kConcert: opening = r.In(20, 40); intro = 0; centres = {length * r.In(.75, .88)}; break;
        case Kind::kMidAction: intro = 0; centres = {length * r.In(.4, .85)}; break;
        case Kind::kSoundOnly: sound_only = true; centres = {length * r.In(.4, .85)}; break;
        case Kind::kMotionOnly: no_sound = true; centres = {length * r.In(.4, .85)}; break;
        case Kind::kCount: break;
    }
    if (plateau_half > 0) half = plateau_half;
    if (ramp_end > 0) tail = ramp_end;
    else if (!centres.empty() && kind != Kind::kEarly && kind != Kind::kAtEnd && kind != Kind::kMidAction &&
             kind != Kind::kAtStart)
        tail = centres.back() + half;
    if (intro > 0) c.intro_s = intro;
    if (ramp_end > 0) c.climaxes.push_back({ramp_end - 5});  // the top 10 s of the rise
    // Two bursts closer than the 20 s between climaxes are one climax, at either.
    if (centres.size() == 2 && centres[1] - centres[0] < 20) c.climaxes.push_back(centres);
    else if (kind != Kind::kAtStart)
        for (double t : centres) c.climaxes.push_back({t});
    const double jitter = noise ? 1.0 : 0.3;
    double walk = base_m;
    for (double t = 0; t < length; t += 0.5) {
        double m = base_m, l = base_l;
        if (drift) {
            walk = std::clamp(walk + 0.01 * (r.Next() - 0.5), 0.05, 0.4);
            m = walk;
            l = 0.3;
        } else if (noise) {
            // nothing but the base level
        } else if (t < opening) {
            m = base_m * 1.75;
            l = 0.95;
        } else if (t < intro) {
            m = 0.1 * base_m;
            l = talky ? r.In(.7, .9) : 0.6 * base_l;
        } else if (tail >= 0 && t >= tail) {
            m = 0.25 * base_m;
            l = 0.3 * base_l;
        } else if (ramp_end > 0) {
            const double u = (t - intro) / (ramp_end - intro);
            m = base_m * 0.5 + u * (peak_m - base_m * 0.5);
            l = base_l + u * (peak_l - base_l);
        } else if (std::any_of(centres.begin(), centres.end(), [&](double x) { return std::abs(t - x) < half; })) {
            m = plateau_half > 0 ? base_m * 1.75 : sound_only ? base_m : peak_m;
            l = plateau_half > 0 ? 0.8 : peak_l;
        }
        if (flat_music) l = music;
        c.s.luma.push_back(t >= black_from && t < black_to ? 0.0 : 0.5);
        c.s.motion.push_back(std::max(0.0, m * (1 + jitter * (r.Next() - 0.5))));
        c.s.loudness.push_back(std::clamp(l * (1 + jitter * (r.Next() - 0.5)), 0.0, 1.0));
    }
    if (no_sound) c.s.loudness.clear();
    return c;
}

// Random arcs, drawn from a family (a stronger test than hand-made shapes).
// The action is piecewise linear through 3-8 knots that vary +-15 %; jitter
// is +-5 to +-25 % per clip (+-50 % in the two noisy families); the loudness
// is divided by its loudest step, as the label editor does.
enum class Family {
    kMixed, kMotionPeak, kSoundPeak, kStairTop, kMixedNoisy,  // one known strongest peak
    kIntroFlat, kFlatTail, kWalkSlow, kWalkMid, kWalkFast, kWalkNoisy, kBeats,  // no peak
    kCount
};
const char* Name(Family f) {
    switch (f) {
        case Family::kMixed: return "peak in motion and sound";
        case Family::kMotionPeak: return "peak in the motion only";
        case Family::kSoundPeak: return "peak in the sound only";
        case Family::kStairTop: return "staircase up to a top";
        case Family::kMixedNoisy: return "peak in motion and sound, jitter +-50 %";
        case Family::kIntroFlat: return "no peak: calm intro, even action";
        case Family::kFlatTail: return "no peak: even action, calm end";
        case Family::kWalkSlow: return "no peak: random walk, step 0.005";
        case Family::kWalkMid: return "no peak: random walk, step 0.01";
        case Family::kWalkFast: return "no peak: random walk, step 0.02";
        case Family::kWalkNoisy: return "no peak: random walk, step 0.01, jitter +-50 %";
        case Family::kBeats: return "no peak: 1 Hz beats in the sound";
        case Family::kCount: break;
    }
    return "";
}
bool HasPeak(Family f) { return int(f) <= int(Family::kMixedNoisy); }

// xorshift64: the families' own generator, seeded per family and split.
struct Xs {
    uint64_t s;
    double U() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return double(s >> 11) / 9007199254740992.0;
    }
    double In(double lo, double hi) { return lo + (hi - lo) * U(); }
};

Clip MakeFamily(Family f, Xs& r) {
    Clip c;
    c.s.step_ms = 500;
    const bool noisy = f == Family::kMixedNoisy || f == Family::kWalkNoisy;
    const double length = r.In(90, 900), base_m = r.In(.15, .25), base_l = r.In(.25, .4);
    const double jitter = noisy ? 1.0 : r.In(.1, .5);
    const bool has_intro = f == Family::kIntroFlat || (f != Family::kFlatTail && r.U() < .5);
    const double intro = has_intro ? r.In(5, 30) : 0;
    const bool tail = f == Family::kFlatTail || (HasPeak(f) && f != Family::kStairTop && r.U() < .3);
    const int knots = 3 + int(r.U() * 6);
    std::vector<double> kt, km, kl;
    for (int i = 0; i <= knots; ++i) {
        kt.push_back(intro + (length - intro) * i / knots);
        km.push_back(base_m * r.In(.85, 1.15));
        kl.push_back(base_l * r.In(.85, 1.15));
    }
    // The peak: a top of half-width w with ramps of ra / rb seconds.
    const double w = r.In(4, 15), ra = r.In(1, 15), rb = r.In(1, 15);
    const double centre = r.In(intro + w + ra + 5, std::max(intro + w + ra + 6, length - w - rb - 3));
    const double gain_m = f == Family::kSoundPeak ? r.In(1.0, 1.1) : r.In(1.5, 2.3);
    const double peak_l = f == Family::kMotionPeak ? -1 : r.In(.7, .95);
    const double tail_from = f == Family::kFlatTail ? length - r.In(15, 60) : centre + w + rb + r.In(5, 30);
    // The staircase: 3-6 steps, each 1.15-1.4x the one before, the top one 15-40 s to the end.
    const int steps = 3 + int(r.U() * 4);
    const double top_len = r.In(15, 40), step_gain = r.In(1.15, 1.4);
    const double step_len = (length - intro - top_len) / (steps - 1);
    const double walk = f == Family::kWalkSlow ? .005 : f == Family::kWalkFast ? .02 : .01;
    const bool no_sound = f == Family::kMotionPeak && r.U() < .5;
    if (has_intro) c.intro_s = intro;
    if (f == Family::kStairTop) c.climaxes.push_back({length - top_len / 2});
    else if (HasPeak(f)) c.climaxes.push_back({centre});
    if (HasPeak(f) && !g_strict_hits) c.top_half_s = f == Family::kStairTop ? top_len / 2 : w;
    double wm = base_m, wl = base_l;
    for (double t = 0; t < length; t += 0.5) {
        double m, l;
        size_t i = 1;
        while (i + 1 < kt.size() && kt[i] < t) ++i;
        const double u = std::clamp((t - kt[i - 1]) / (kt[i] - kt[i - 1]), 0.0, 1.0);
        m = km[i - 1] + u * (km[i] - km[i - 1]);
        l = kl[i - 1] + u * (kl[i] - kl[i - 1]);
        if (f == Family::kWalkSlow || f == Family::kWalkMid || f == Family::kWalkFast || f == Family::kWalkNoisy) {
            wm = std::clamp(wm + walk * (r.U() - .5), .05, .4);
            wl = std::clamp(wl + walk * (r.U() - .5), .1, .6);
            m = wm;
            l = wl;
        } else if (f == Family::kBeats) {
            l = std::fmod(t, 1.0) < .5 ? .7 : .2;
        } else if (f == Family::kStairTop) {
            const int k = std::min(steps - 1, int((t - intro) / step_len));
            m = base_m * std::pow(step_gain, k);
            l = std::min(.95, base_l * std::pow(step_gain, k));
        }
        if (t >= intro && tail && t >= tail_from) {
            m = .25 * base_m;
            l = .3 * base_l;
        }
        if (HasPeak(f) && f != Family::kStairTop) {
            const double d = std::abs(t - centre);
            const double share = d <= w ? 1 : t < centre ? 1 - (d - w) / ra : 1 - (d - w) / rb;
            if (share > 0) {
                m += share * (m * gain_m - m);
                if (peak_l > 0) l += share * std::max(0.0, peak_l - l);
            }
        }
        if (t < intro) {
            m = .1 * base_m;
            l = .6 * base_l;
        }
        c.s.luma.push_back(0.5);
        c.s.motion.push_back(std::max(0.0, m * (1 + jitter * (r.U() - .5))));
        c.s.loudness.push_back(std::clamp(l * (1 + jitter * (r.U() - .5)), 0.0, 1.0));
    }
    if (no_sound) c.s.loudness.clear();
    const double top = c.s.loudness.empty() ? 0 : *std::max_element(c.s.loudness.begin(), c.s.loudness.end());
    if (top > 0)
        for (auto& v : c.s.loudness) v /= top;
    return c;
}

constexpr double kClimaxHitS = 5;  // the acceptance of D-165
constexpr double kIntroHitS = 3;
constexpr int kTimingRuns = 20;
// More false climaxes per clip than the old rules plus this fail the bench,
// fewer intro hits than theirs less this share of the clips too.
constexpr double kFalseTolerance = 0.02;
constexpr double kIntroTolerance = 0.02;
// Where the old rules suggest more than this many false climaxes per clip
// (fast random walks), neither rule set is usable: the bound is theirs plus
// kUnusableTolerance. Suggestions are never applied without the user.
constexpr double kUnusableRate = 0.25, kUnusableTolerance = 0.10;
// The families' seeds: constants were chosen on the train set; the held-out
// set was looked at once, for the final report.
constexpr uint64_t kTrainSeed = 0x7a11e5ull, kHeldOutSeed = 0x4e1d0a7ull;

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
    const auto off = [&](double m, const std::vector<double>& truth) {
        double best = 1e9;
        for (double t : truth) best = std::min(best, std::abs(m - t));
        return best;
    };
    const double reach = std::max(kClimaxHitS, c.top_half_s);
    for (const auto& truth : c.climaxes) {
        ++s.climaxes;
        double best = 1e9;
        for (double m : mids) best = std::min(best, off(m, truth));
        if (best < 1e9) s.climax_err.push_back(best);
        if (best <= reach) ++s.climax_hits;
    }
    for (double m : mids)
        if (std::none_of(c.climaxes.begin(), c.climaxes.end(), [&](const auto& t) { return off(m, t) <= reach; }))
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

bool NoWorse(const Score& arc, const Score& before) {
    const double clips = std::max(1, arc.clips), old_rate = double(before.false_climaxes) / clips;
    const double tolerance = old_rate > kUnusableRate ? kUnusableTolerance : kFalseTolerance;
    return arc.climax_hits >= before.climax_hits && double(arc.false_climaxes) / clips <= old_rate + tolerance &&
           arc.intro_hits >= before.intro_hits - kIntroTolerance * clips;
}

int main(int argc, char** argv) {
    int variants = 25, family_clips = 300;
    std::string split = "both";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--variants") == 0) variants = std::max(1, std::atoi(argv[i + 1]));
        if (std::strcmp(argv[i], "--family-clips") == 0) family_clips = std::max(1, std::atoi(argv[i + 1]));
        if (std::strcmp(argv[i], "--split") == 0) split = argv[i + 1];
    }
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--strict-hits") == 0) g_strict_hits = true;
    const auto now = [](const PhaseSignals& s) { return samn::SuggestPhases(s); };
    const auto before = [](const PhaseSignals& s) { return legacy::LegacyRules(s); };
    std::vector<std::string> worse;

    std::printf("%d clips per scenario, climax hit within %.0f s of the truth (midpoint of a suggested climax), "
                "intro hit within %.0f s (no intro in the truth: none suggested)\n\n",
                variants, kClimaxHitS, kIntroHitS);
    std::printf("| Scenario | Rules | Climax hits | Median climax error (s) | False climaxes | Intro hits | "
                "Median intro error (s) | CPU us / call |\n|---|---|---|---|---|---|---|---|\n");
    Score total_before, total_now;
    for (int k = 0; k < int(Kind::kCount); ++k) {
        const Kind kind = Kind(k);
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
        if (!NoWorse(a, b)) worse.push_back(std::string("scenario: ") + Name(kind));
    }
    Row("**all**", "before", total_before);
    Row("**all**", "arc", total_now);

    for (const char* part : {"train", "held-out"}) {
        if (split != "both" && split != part) continue;
        const uint64_t seed = std::strcmp(part, "train") == 0 ? kTrainSeed : kHeldOutSeed;
        std::printf("\n%d random arcs per family, %s seeds; a climax is hit within 5 s of the top's centre%s\n\n",
                    family_clips, part, g_strict_hits ? "" : " or anywhere on a wider flat top");
        std::printf("| Family | Climax hits before | Climax hits arc | False climaxes / clip before | "
                    "False climaxes / clip arc | Intro hits before | Intro hits arc |\n|---|---|---|---|---|---|---|\n");
        for (int k = 0; k < int(Family::kCount); ++k) {
            const Family f = Family(k);
            Xs r{seed + uint64_t(k) * 1000003ull};
            Score b, a;
            for (int v = 0; v < family_clips; ++v) {
                const Clip c = MakeFamily(f, r);
                Grade(c, before(c.s), b);
                Grade(c, now(c.s), a);
            }
            const auto pct = [](const Score& s) { return s.climaxes ? 100.0 * s.climax_hits / s.climaxes : 0.0; };
            std::printf("| %s | %s | %s | %.3f | %.3f | %d/%d | %d/%d |\n", Name(f),
                        HasPeak(f) ? (std::to_string(int(pct(b) + .5)) + " %").c_str() : "-",
                        HasPeak(f) ? (std::to_string(int(pct(a) + .5)) + " %").c_str() : "-",
                        double(b.false_climaxes) / b.clips, double(a.false_climaxes) / a.clips, b.intro_hits, b.clips,
                        a.intro_hits, a.clips);
            if (!NoWorse(a, b)) worse.push_back(std::string(part) + ": " + Name(f));
        }
    }
    std::printf("\nWorse than the old rules (fewer climax hits, intro hits more than %.0f %% fewer, or more false "
                "climaxes per clip than +%.2f, +%.2f where theirs exceed %.2f): %s\n",
                100 * kIntroTolerance, kFalseTolerance, kUnusableTolerance, kUnusableRate, worse.empty() ? "none" : "");
    for (const auto& w : worse) std::printf("- %s\n", w.c_str());
    return worse.empty() ? 0 : 1;
}
