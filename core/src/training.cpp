#include "samn/training.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace samn::training {

using nlohmann::json;

// ---------------------------------------------------------------- names

std::string to_string(Technique t) { return t == Technique::Plateau ? "plateau" : "stopstart"; }
std::string to_string(Channel c) {
    switch (c) {
        case Channel::Suction: return "suction";
        case Channel::Both: return "both";
        default: return "vibration";
    }
}
Technique technique_from(const std::string& s) { return s == "plateau" ? Technique::Plateau : Technique::StopStart; }
Channel channel_from(const std::string& s) {
    if (s == "suction") return Channel::Suction;
    if (s == "both") return Channel::Both;
    return Channel::Vibration;
}

// ---------------------------------------------------------------- json

void to_json(json& j, const ChannelCurve& c) {
    j = json{{"startLevel", c.start_level}, {"peakLevel", c.peak_level}, {"endLevel", c.end_level},
             {"rampUpMs", c.ramp_up_ms},    {"holdMs", c.hold_ms},       {"rampDownMs", c.ramp_down_ms},
             {"randomJitterFraction", c.random_jitter_fraction}};
    if (c.start_from_current) j["startFromCurrent"] = true;
}
void from_json(const json& j, ChannelCurve& c) {
    c = ChannelCurve{};
    c.start_level = j.value("startLevel", 0.0);
    c.peak_level = j.value("peakLevel", 0.0);
    c.end_level = j.value("endLevel", 0.0);
    c.ramp_up_ms = j.value("rampUpMs", 0);
    c.hold_ms = j.value("holdMs", 0);
    c.ramp_down_ms = j.value("rampDownMs", 0);
    c.random_jitter_fraction = j.value("randomJitterFraction", 0.0);
    c.start_from_current = j.value("startFromCurrent", false);
}
void to_json(json& j, const Phase& p) {
    j = json{{"name", p.name}, {"repeatCycles", p.repeat_cycles}, {"restMs", p.rest_ms}};
    if (p.vibration) j["vibration"] = *p.vibration;
    if (p.suction) j["suction"] = *p.suction;
}
void from_json(const json& j, Phase& p) {
    p = Phase{};
    p.name = j.value("name", std::string());
    p.repeat_cycles = j.value("repeatCycles", 0);
    p.rest_ms = j.value("restMs", 0);
    if (j.contains("vibration") && j["vibration"].is_object()) p.vibration = j["vibration"].get<ChannelCurve>();
    if (j.contains("suction") && j["suction"].is_object()) p.suction = j["suction"].get<ChannelCurve>();
}
void to_json(json& j, const Script& s) {
    j = json{{"name", s.name}, {"description", s.description}, {"phases", s.phases},
             {"progressionPerCycle", s.progression_per_cycle}};
    if (!s.category.empty()) j["category"] = s.category;
    if (s.level != 0) j["level"] = s.level;
}
void from_json(const json& j, Script& s) {
    s = Script{};
    s.name = j.value("name", std::string());
    s.description = j.value("description", std::string());
    s.category = j.value("category", std::string());
    s.level = j.value("level", 0);
    s.progression_per_cycle = j.value("progressionPerCycle", 0.0);
    if (j.contains("phases") && j["phases"].is_array()) s.phases = j["phases"].get<std::vector<Phase>>();
}

// ---------------------------------------------------------------- numbers

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
double clamp_range(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

Adjusted adjust_for_arousal(int arousal, double peak, int hold_ms, int rest_ms) {
    if (arousal < 1) return {peak, hold_ms, rest_ms};
    const double deviation = arousal - kArousalTarget;
    double hold_f = 1, rest_f = 1, peak_f = 1;
    if (deviation > 0) {
        hold_f = clamp_range(1.0 - 0.15 * deviation, 0.35, 1.0);
        rest_f = clamp_range(1.0 + 0.20 * deviation, 1.0, 2.5);
        peak_f = clamp_range(1.0 - 0.07 * deviation, 0.5, 1.0);
    } else if (deviation < 0) {
        hold_f = clamp_range(1.0 - 0.07 * deviation, 1.0, 1.3);
    }
    return {clamp01(peak * peak_f), static_cast<int>(hold_ms * hold_f), static_cast<int>(rest_ms * rest_f)};
}

ArousalFactors arousal_factors(int arousal) {
    const Adjusted a = adjust_for_arousal(arousal, 1.0, 1000, 1000);
    return {a.peak, a.hold_ms / 1000.0, a.rest_ms / 1000.0};
}

// ---------------------------------------------------------------- scripts

namespace {

ChannelCurve curve(double start, double peak, double end, int up, int hold, int down) {
    ChannelCurve c;
    c.start_level = start;
    c.peak_level = peak;
    c.end_level = end;
    c.ramp_up_ms = up;
    c.hold_ms = hold;
    c.ramp_down_ms = down;
    return c;
}
ChannelCurve from_current(ChannelCurve c) {
    c.start_from_current = true;
    return c;
}
ChannelCurve with_jitter(ChannelCurve c, double f) {
    c.random_jitter_fraction = f;
    return c;
}
Phase phase(const char* name, std::optional<ChannelCurve> vib, std::optional<ChannelCurve> suc, int repeats,
            int rest) {
    Phase p;
    p.name = name;
    p.vibration = vib;
    p.suction = suc;
    p.repeat_cycles = repeats;
    p.rest_ms = rest;
    return p;
}
Script script(const char* name, const char* category, int level, const char* description, std::vector<Phase> phases,
              double progression = 0) {
    Script s;
    s.name = name;
    s.category = category;
    s.level = level;
    s.description = description;
    s.phases = std::move(phases);
    s.progression_per_cycle = progression;
    return s;
}

std::vector<Script> make_builtins() {
    // Defaults match the Custom form so classic methods and presets stay one family.
    constexpr double classic_peak = 0.8, plateau_frac = 0.7, light_suction = 0.4;
    constexpr int up = 8000, hold = 3000, rest = 10000, cycles = 5;
    std::vector<Script> v;
    v.push_back(script(
        "stop-start", kCategoryControl, 1,
        "Classic Stop-Start (Semans): ramp to peak, hold, fully to 0, rest, with a lighter suction layer riding "
        "along. Same method as Custom \xE2\x86\x92 Technique Stop-start; Vibration. Fine-tune timings under Custom.",
        {phase("Stop-Start cycle", curve(0, classic_peak, 0, up, hold, up),
               curve(0, classic_peak * light_suction, 0, up, hold, up), cycles, rest)},
        0.15));
    v.push_back(script(
        "plateau", kCategoryEdge, 1,
        "Classic Plateau/edging: ramp to peak, hold, down to a high floor (not 0), rest, with a lighter suction "
        "layer that (unlike vibration) fully releases each cycle. Same method as Custom \xE2\x86\x92 Technique "
        "Plateau; Vibration. Fine-tune under Custom.",
        {phase("Plateau cycle", from_current(curve(0, classic_peak, classic_peak * plateau_frac, up, hold, up)),
               curve(0, classic_peak * light_suction, 0, up, hold, up), cycles, rest)},
        0.15));
    v.push_back(script(
        "vibration-wave-suction-focus", kCategoryEndurance, 2,
        "Light vibration ramps up then back down, then a suction-focused phase carries a light constant vibration "
        "alongside it.",
        {phase("Vibration wave", curve(0.2, 0.8, 0.2, 6000, 2000, 6000), std::nullopt, 3, 4000),
         phase("Suction focus", std::nullopt, curve(0.2, 0.75, 0.3, 5000, 3000, 5000), 3, 5000)}));
    v.push_back(script(
        "tissue-massage", kCategoryBody, 1,
        "Suction-only, cupping-inspired: fast pumping, a long static hold, then a slow gliding sweep. Vibration "
        "stays off.",
        {phase("Pumping", std::nullopt, curve(0, 0.6, 0, 400, 200, 400), 8, 300),
         phase("Static hold", std::nullopt, curve(0, 0.6, 0.6, 3000, 6000, 0), 1, 3000),
         phase("Gliding sweep", std::nullopt, curve(0.2, 0.8, 0.2, 15000, 0, 15000), 1, 0)}));
    v.push_back(script(
        "vibration-massage", kCategoryEndurance, 1,
        "Vibration-led: a gentle escalating warm-up, then a wave rhythm with no hard hold, with a light suction "
        "layer under both.",
        {phase("Escalating warm-up", curve(0.15, 0.5, 0.3, 4000, 1500, 3000), curve(0.05, 0.25, 0.15, 4000, 1500, 3000),
               4, 2000),
         phase("Wave", curve(0.2, 0.85, 0.2, 3000, 0, 3000), curve(0.1, 0.35, 0.1, 3000, 0, 3000), 6, 1000)},
        0.2));
    v.push_back(script(
        "variable", kCategoryControl, 2,
        "Wave rhythm with randomized peak per repeat - reduces sensory adaptation instead of a fully predictable "
        "rhythm - with a light, steadier suction layer alongside.",
        {phase("Randomized wave", with_jitter(curve(0.2, 0.75, 0.2, 3000, 500, 3000), 0.25),
               curve(0.1, 0.3, 0.1, 3000, 500, 3000), 8, 2000)}));
    v.push_back(script(
        "pulse-rhythm", kCategoryEdge, 2,
        "Sharp on/off pulses (near-instant ramps): a fast vibration pulse phase, then a fast suction pulse phase - "
        "punchier and more staccato than the wave-based scripts above.",
        {phase("Vibration pulses", curve(0, 0.7, 0, 80, 150, 80), std::nullopt, 6, 200),
         phase("Suction pulses", std::nullopt, curve(0, 0.65, 0, 100, 150, 100), 6, 200)}));
    v.push_back(script(
        "endurance-ladder", kCategoryEndurance, 2,
        "Level 2 endurance ladder: Warm Up, Build, Peak, then Cool Down. Longer rests between builds; light suction "
        "rides along. Not a game sync \xE2\x80\x94 device patterns only.",
        {phase("Warm Up", curve(0.1, 0.4, 0.15, 5000, 2000, 4000), curve(0.05, 0.2, 0.08, 5000, 2000, 4000), 3, 3000),
         phase("Build", curve(0.2, 0.65, 0.25, 6000, 2500, 5000), curve(0.1, 0.3, 0.12, 6000, 2500, 5000), 4, 4000),
         phase("Peak", curve(0.3, 0.85, 0.3, 5000, 3000, 5000), curve(0.12, 0.35, 0.15, 5000, 3000, 5000), 3, 5000),
         phase("Cool Down", curve(0.25, 0.4, 0, 4000, 1500, 6000), curve(0.1, 0.2, 0, 4000, 1500, 6000), 2, 2000)},
        0.1));
    v.push_back(script(
        "edge-ladder", kCategoryEdge, 3,
        "Level 3 edge ladder: longer plateau holds near the peak, then a short release. Use Interrupt when you need "
        "to stop a cycle. Harder than classic Plateau.",
        {phase("Approach", curve(0.15, 0.6, 0.35, 7000, 2000, 4000), curve(0.08, 0.25, 0, 7000, 2000, 4000), 2, 4000),
         phase("Surf", from_current(curve(0.35, 0.9, 0.9 * plateau_frac, 5000, 8000, 4000)),
               curve(0.1, 0.3, 0, 5000, 2000, 4000), 4, 6000),
         phase("Release", curve(0.4, 0.55, 0, 3000, 1000, 5000), curve(0.1, 0.2, 0, 3000, 1000, 5000), 1, 0)},
        0.08));
    v.push_back(script(
        "breathing-pace", kCategoryBody, 1,
        "Breathing pace (about 6 breaths per minute): soft vibration rise on the in-breath, ease on the out-breath, "
        "light suction alongside. Follow your own breathing; this is a device pattern, not medical advice and not "
        "biofeedback.",
        {phase("Breathe with the wave", curve(0.05, 0.45, 0.05, 4500, 500, 4500),
               curve(0.05, 0.2, 0.05, 4500, 500, 4500), 10, 500)}));
    v.push_back(script(
        "pelvic-floor", kCategoryBody, 1,
        "Pelvic-floor style squeeze and release: short suction holds with a light vibration tick. Contract with the "
        "hold, relax on rest. Device cues only \xE2\x80\x94 not a medical Kegel trainer and not biofeedback.",
        {phase("Squeeze", curve(0, 0.25, 0, 300, 2500, 400), curve(0, 0.55, 0, 300, 2500, 400), 8, 3000),
         phase("Quick pulses", curve(0, 0.2, 0, 150, 200, 150), curve(0, 0.45, 0, 150, 200, 150), 10, 800)}));
    return v;
}

ChannelCurve scale_levels(ChannelCurve c, double f) {
    c.start_level = clamp01(c.start_level * f);
    c.peak_level = clamp01(c.peak_level * f);
    c.end_level = clamp01(c.end_level * f);
    return c;
}

}  // namespace

const std::vector<Script>& builtin_scripts() {
    static const std::vector<Script> all = make_builtins();
    return all;
}

const Script* builtin_script(const std::string& name) {
    for (const auto& s : builtin_scripts())
        if (s.name == name) return &s;
    return nullptr;
}

Script scale_script(const Script& s, double factor) {
    if (factor <= 0 || factor == 1) return s;
    Script out = s;
    for (auto& p : out.phases) {
        if (p.vibration) p.vibration = scale_levels(*p.vibration, factor);
        if (p.suction) p.suction = scale_levels(*p.suction, factor);
    }
    return out;
}

std::string validate_script(const Script& s) {
    if (s.phases.empty()) return "a script needs at least 1 phase";
    for (const auto& p : s.phases) {
        if (p.repeat_cycles <= 0) return "phase \"" + p.name + "\" needs at least 1 repeat";
        if (!p.vibration && !p.suction) return "phase \"" + p.name + "\" has no active channel";
    }
    return {};
}

std::string validate_options(const Options& o) { return o.cycles <= 0 ? "at least 1 cycle required" : std::string(); }

ChannelCurve scaled_curve(const ChannelCurve& base, double progress, double peak_factor, double hold_factor,
                          const std::function<double()>& rng) {
    double peak = clamp01(base.peak_level * progress * peak_factor);
    if (base.random_jitter_fraction > 0) {
        const double delta = (rng() * 2 - 1) * base.random_jitter_fraction;
        peak = clamp01(peak * (1 + delta));
    }
    double start = base.start_level, end = base.end_level;
    if (base.peak_level > 0) {
        // Start and end follow the SCALED peak, so damping lowers the whole curve.
        const double ratio = peak / base.peak_level;
        start = clamp01(base.start_level * ratio);
        end = clamp01(base.end_level * ratio);
    }
    ChannelCurve c;
    c.start_level = start;
    c.peak_level = peak;
    c.end_level = end;
    c.ramp_up_ms = base.ramp_up_ms;
    c.hold_ms = static_cast<int>(base.hold_ms * hold_factor);
    c.ramp_down_ms = base.ramp_down_ms;
    c.start_from_current = base.start_from_current;
    return c;
}

// ---------------------------------------------------------------- preview

namespace {
int append_curve(std::vector<CurvePoint>& pts, const std::optional<ChannelCurve>& c, int at, double& last) {
    if (!c) return at;
    const double start = c->start_from_current ? last : c->start_level;
    pts.push_back({at, start});
    at += c->ramp_up_ms;
    pts.push_back({at, c->peak_level});
    at += c->hold_ms;
    pts.push_back({at, c->peak_level});
    at += c->ramp_down_ms;
    pts.push_back({at, c->end_level});
    last = c->end_level;
    return at;
}
}  // namespace

Preview build_preview(const Script& s) {
    Preview r;
    int t = 0;
    double last_vib = 0, last_suc = 0;
    for (const auto& p : s.phases) {
        const auto jitter = [](const std::optional<ChannelCurve>& c) { return c && c->random_jitter_fraction > 0; };
        if (jitter(p.vibration) || jitter(p.suction)) r.has_random_jitter = true;
        r.markers.push_back({t, p.name});
        for (int i = 0; i < p.repeat_cycles; ++i) {
            const int vib_end = append_curve(r.vibration, p.vibration, t, last_vib);
            const int suc_end = append_curve(r.suction, p.suction, t, last_suc);
            const int end = std::max(vib_end, suc_end);
            if (!p.vibration) r.vibration.push_back({end, last_vib});
            if (!p.suction) r.suction.push_back({end, last_suc});
            t = end + p.rest_ms;
        }
    }
    r.total_ms = t;
    return r;
}

Preview build_preview(const Options& o) {
    Preview r;
    const bool vib = o.channel != Channel::Suction, suc = o.channel != Channel::Vibration;
    int t = 0;
    double level = 0;
    for (int i = 0; i < o.cycles; ++i) {
        const double frac = o.cycles > 1 ? double(i) / (o.cycles - 1) : 0;
        const double progress = 1.0 + o.progression_per_cycle * frac;
        const double peak = clamp01(o.peak_intensity * progress);
        const int hold = static_cast<int>(o.hold_ms * progress);
        const double target = o.technique == Technique::Plateau ? clamp01(peak * o.plateau_fraction) : 0;
        r.markers.push_back({t, "Cycle " + std::to_string(i + 1)});
        std::vector<CurvePoint> pts;
        pts.push_back({t, level});
        t += o.ramp_up_ms;
        pts.push_back({t, peak});
        t += hold;
        pts.push_back({t, peak});
        t += o.rest_ms;
        pts.push_back({t, target});
        level = target;
        if (o.technique != Technique::Plateau) {  // stop-start: a full rest at 0 after the ramp down
            t += o.rest_ms;
            pts.push_back({t, 0});
        }
        if (vib) r.vibration.insert(r.vibration.end(), pts.begin(), pts.end());
        if (suc) r.suction.insert(r.suction.end(), pts.begin(), pts.end());
    }
    r.total_ms = t;
    return r;
}

namespace {
// Level of a piecewise-linear plan at a time (held after the last point). At a jump (two
// points at one time) `after` picks the level after it, else the level before it; so a phase
// starts at its own first level, not at the end level of the phase before (0.7 used the latter).
double level_at(const std::vector<CurvePoint>& pts, double ms, bool after) {
    for (size_t i = 1; i < pts.size(); ++i) {
        const auto &a = pts[i - 1], &b = pts[i];
        if (after ? ms < b.at_ms : ms <= b.at_ms) {
            const double f = b.at_ms > a.at_ms ? (ms - a.at_ms) / (b.at_ms - a.at_ms) : 0;
            return a.level + (b.level - a.level) * clamp01(f);
        }
    }
    return pts.empty() ? 0 : pts.back().level;
}
double mean_level(const std::vector<CurvePoint>& pts, int from, int to) {
    if (pts.size() < 2 || to <= from) return 0;
    std::vector<std::pair<double, double>> s{{double(from), level_at(pts, from, true)}};
    for (const auto& p : pts)
        if (p.at_ms > from && p.at_ms < to) s.emplace_back(p.at_ms, p.level);
    s.emplace_back(double(to), level_at(pts, to, false));
    double area = 0;
    for (size_t i = 1; i < s.size(); ++i) area += (s[i].first - s[i - 1].first) * (s[i].second + s[i - 1].second) / 2;
    return area / (to - from);
}
}  // namespace

double phase_strength(const Preview& p, int from_ms, int to_ms) {
    return std::max(mean_level(p.vibration, from_ms, to_ms), mean_level(p.suction, from_ms, to_ms));
}

// ---------------------------------------------------------------- session

Session::Session(Device* dev, Rng rng) : dev_(dev), rng_(std::move(rng)) {
    if (!rng_) {
        auto gen = std::make_shared<std::mt19937_64>(std::random_device{}());
        rng_ = [gen] { return std::uniform_real_distribution<double>(0.0, 1.0)(*gen); };
    }
}
Session::~Session() = default;

std::unique_ptr<Session> Session::custom(const Options& o, Device* dev, std::string* error, Rng rng) {
    if (const std::string e = validate_options(o); !e.empty()) {
        if (error) *error = e;
        return nullptr;
    }
    std::unique_ptr<Session> s(new Session(dev, std::move(rng)));
    s->opts_ = o;
    s->stage_ = Stage::Begin;
    s->pos_.phases_total = 1;
    s->pos_.repeats_total = o.cycles;
    return s;
}

std::unique_ptr<Session> Session::script(const Script& in, Device* dev, std::string* error, Rng rng) {
    Script sc = in;
    if (const std::string e = validate_script(sc); !e.empty()) {
        if (error) *error = e;
        return nullptr;
    }
    std::unique_ptr<Session> s(new Session(dev, std::move(rng)));
    s->script_mode_ = true;
    for (const auto& p : sc.phases) s->total_repeats_ += p.repeat_cycles;
    s->script_ = std::move(sc);
    s->pos_.phases_total = int(s->script_.phases.size());
    return s;
}

void Session::notify() {
    if (on_levels) on_levels(vibration(), suction());
}

void Session::write(bool vib, bool suc, double level) {
    if (vib) {
        if (!held_) dev_->set_vibration(level);
        vib_ = level;
    }
    if (suc) {
        if (!held_) dev_->set_suction(level);
        suc_ = level;
    }
    notify();
}

void Session::ramp(Lane& l, bool vib, bool suc, double from, double to, int ms) {
    if (ms <= 0) {
        l.ops.push_back({Op::Write, vib, suc, to});
        return;
    }
    const int steps = std::max(1, ms / 50);
    const double step_ms = double(ms) / steps;
    for (int i = 1; i <= steps; ++i) {
        l.ops.push_back({Op::Check, false, false, 0});
        l.ops.push_back({Op::Write, vib, suc, from + (to - from) * i / steps});
        l.ops.push_back({Op::Wait, false, false, step_ms});
    }
    l.ops.push_back({Op::Check, false, false, 0});
}

void Session::hold(Lane& l, double ms) {
    for (double remaining = ms; remaining > 0; remaining -= 50) {
        l.ops.push_back({Op::Check, false, false, 0});
        l.ops.push_back({Op::Wait, false, false, std::min(remaining, 50.0)});
    }
    l.ops.push_back({Op::Check, false, false, 0});
}

void Session::wait(Lane& l, double ms) {
    if (ms > 0) l.ops.push_back({Op::Wait, false, false, ms});
}

void Session::start(Lane& l) {
    l.pos = 0;
    l.left = 0;
    l.waiting = false;
    l.stopped = false;
    l.done = false;
}

double Session::run_lane(Lane& l, double budget) {
    double used = 0;
    while (!l.done) {
        if (l.pos >= l.ops.size()) {
            l.done = true;
            break;
        }
        const Op& op = l.ops[l.pos];
        switch (op.kind) {
            case Op::Write: write(op.vib, op.suc, op.v); ++l.pos; break;
            case Op::Check:
                if (stop_requested_) {
                    l.stopped = true;
                    l.done = true;
                } else {
                    ++l.pos;
                }
                break;
            case Op::Wait: {
                if (!l.waiting) {
                    l.left = op.v;
                    l.waiting = true;
                }
                const double take = std::min(l.left, budget - used);
                l.left -= take;
                used += take;
                if (l.left > 1e-9) return used;  // budget spent
                l.waiting = false;
                ++l.pos;
                break;
            }
        }
    }
    return used;
}

void Session::advance(double dt_ms) {
    if (finished_ || paused_) return;
    double remaining = std::max(0.0, dt_ms);
    while (!finished_) {
        bool changed = false;
        const double used = step(remaining, changed);
        clock_ += used;
        remaining -= used;
        if (!changed && (remaining <= 1e-9 || used <= 1e-12)) break;
    }
}

double Session::step(double budget, bool& changed) {
    return script_mode_ ? step_script(budget, changed) : step_custom(budget, changed);
}

int Session::take_arousal() {
    const int a = arousal_;
    arousal_ = 0;
    return a;
}

void Session::stop_cycle() { stop_requested_ = true; }

void Session::report_arousal(int level) {
    if (level < 1 || level > 10) return;  // ignored, not clamped: an empty field must not read as "relaxed"
    arousal_ = level;
}

void Session::pause() {
    if (paused_ || finished_) return;
    paused_ = true;
    held_ = true;
    dev_->set_vibration(0);
    dev_->set_suction(0);
    notify();
}

void Session::resume() {
    if (!paused_) return;
    paused_ = false;
    held_ = false;
    dev_->set_vibration(vib_);
    dev_->set_suction(suc_);
    notify();
}

void Session::finish() {
    finished_ = true;
    dev_->stop();
}

void Session::abort() {
    if (finished_) return;
    finish();
}

// ---- script stages

void Session::begin_repeat() {
    const Phase& ph = script_.phases[pi_];
    const double frac = total_repeats_ > 1 ? double(global_) / (total_repeats_ - 1) : 0.0;
    const double progress = 1.0 + script_.progression_per_cycle * frac;
    const int arousal = take_arousal();
    const ArousalFactors f = arousal_factors(arousal);

    cur_vib_.reset();
    cur_suc_.reset();
    if (ph.vibration) cur_vib_ = scaled_curve(*ph.vibration, progress, f.peak, f.hold, rng_);
    if (ph.suction) cur_suc_ = scaled_curve(*ph.suction, progress, f.peak, f.hold, rng_);
    if (cur_vib_ && cur_vib_->start_from_current) cur_vib_->start_level = last_vib_;
    if (cur_suc_ && cur_suc_->start_from_current) cur_suc_->start_level = last_suc_;
    rest_ms_ = static_cast<int>(ph.rest_ms * f.rest);

    stop_requested_ = false;  // drain
    sres_ = ScriptCycleResult{};
    sres_.phase_index = pi_;
    sres_.phase_name = ph.name;
    sres_.phases_total = int(script_.phases.size());
    sres_.repeat_index = ri_;
    sres_.repeats_total = ph.repeat_cycles;
    sres_.rest_ms = rest_ms_;
    sres_.arousal_before = arousal;
    sres_.peak_factor_applied = f.peak;
    sres_.rest_factor_applied = f.rest;
    sres_.started_at_ms = clock_;
    if (cur_vib_) sres_.vibration_peak = cur_vib_->peak_level;
    if (cur_suc_) sres_.suction_peak = cur_suc_->peak_level;

    pos_.phase_index = pi_;
    pos_.phase_name = ph.name;
    pos_.repeat_index = ri_;
    pos_.repeats_total = ph.repeat_cycles;
    pos_.vibration_peak = sres_.vibration_peak;
    pos_.suction_peak = sres_.suction_peak;

    const auto build = [this](Lane& l, const std::optional<ChannelCurve>& c, bool vib) {
        l.ops.clear();
        l.done = true;
        if (!c) return;
        ramp(l, vib, !vib, c->start_level, c->peak_level, c->ramp_up_ms);
        hold(l, c->hold_ms);
        ramp(l, vib, !vib, c->peak_level, c->end_level, c->ramp_down_ms);
        start(l);
    };
    build(vl_, cur_vib_, true);
    build(sl_, cur_suc_, false);
    stage_ = Stage::Repeat;
}

void Session::finish_repeat() {
    sres_.reached_peak_after_ms = static_cast<int>(clock_ - sres_.started_at_ms);
    const bool stopped = vl_.stopped || sl_.stopped;
    sres_.stopped_by_user = stopped;
    if (stopped) {
        write(true, false, 0);
        write(false, true, 0);
        last_vib_ = last_suc_ = 0;
    } else {
        if (cur_vib_) last_vib_ = cur_vib_->end_level;
        if (cur_suc_) last_suc_ = cur_suc_->end_level;
    }
    vl_.ops.clear();
    if (last_vib_ > 0 || last_suc_ > 0) {
        hold(vl_, rest_ms_);  // a channel still sounds in the rest: Interrupt works here too
        start(vl_);
        stage_ = Stage::RestInterruptible;
    } else {
        wait(vl_, rest_ms_);
        start(vl_);
        stage_ = Stage::RestPlain;
    }
}

double Session::step_script(double budget, bool& changed) {
    switch (stage_) {
        case Stage::Begin: {
            if (pi_ >= int(script_.phases.size())) {
                if (last_vib_ > 0 || last_suc_ > 0) {  // wind the elevated channels down over 2 s
                    const auto build = [](Lane& l, double level, bool vib) {
                        l.ops.clear();
                        l.done = true;
                        if (level <= 0) return;
                        ramp(l, vib, !vib, level, level, 0);
                        hold(l, 0);
                        ramp(l, vib, !vib, level, 0, 2000);
                        start(l);
                    };
                    build(vl_, last_vib_, true);
                    build(sl_, last_suc_, false);
                    stage_ = Stage::Closing;
                    changed = true;
                    return 0;
                }
                finish();
                return 0;
            }
            begin_repeat();
            changed = true;
            return 0;
        }
        case Stage::Repeat:
        case Stage::Closing: {
            const double a = run_lane(vl_, budget), b = run_lane(sl_, budget);
            if (!vl_.done || !sl_.done) return std::max(a, b);
            changed = true;
            if (stage_ == Stage::Closing) {
                finish();
            } else {
                finish_repeat();
            }
            return std::max(a, b);
        }
        case Stage::RestInterruptible: {
            const double used = run_lane(vl_, budget);
            if (!vl_.done) return used;
            changed = true;
            if (vl_.stopped) {
                sres_.stopped_by_user = true;
                write(true, false, 0);
                write(false, true, 0);
                last_vib_ = last_suc_ = 0;
                vl_.ops.clear();
                wait(vl_, rest_ms_);
                start(vl_);
                stage_ = Stage::RestPlain;
                return used;
            }
            [[fallthrough]];
        }
        case Stage::RestPlain: {
            const double used = stage_ == Stage::RestPlain ? run_lane(vl_, budget) : 0;
            if (stage_ == Stage::RestPlain && !vl_.done) return used;
            changed = true;
            sres_.ended_at_ms = clock_ + used;
            if (on_script_cycle) on_script_cycle(sres_);
            ++global_;
            if (++ri_ >= script_.phases[pi_].repeat_cycles) {
                ri_ = 0;
                ++pi_;
            }
            stage_ = Stage::Begin;
            return used;
        }
        default: return 0;
    }
}

// ---- custom-form stages

void Session::begin_cycle() {
    const int n = opts_.cycles;
    const double frac = n > 1 ? double(ci_) / (n - 1) : 0.0;
    const double progress = 1.0 + opts_.progression_per_cycle * frac;
    double peak = clamp01(opts_.peak_intensity * progress);
    int hold_ms = static_cast<int>(opts_.hold_ms * progress);
    int rest_ms = opts_.rest_ms;
    const int arousal = take_arousal();
    const Adjusted a = adjust_for_arousal(arousal, peak, hold_ms, rest_ms);
    peak = a.peak;
    hold_ms = a.hold_ms;
    rest_ms = a.rest_ms;

    stop_requested_ = false;  // drain
    cres_ = CycleResult{};
    cres_.cycle_index = ci_;
    cres_.peak_intensity = peak;
    cres_.hold_ms = hold_ms;
    cres_.rest_ms = rest_ms;
    cres_.arousal_before = arousal;
    cres_.started_at_ms = clock_;
    cycle_peak_ = peak;
    rest_ms_ = rest_ms;
    pos_.repeat_index = ci_;
    pos_.phase_name = "Cycle " + std::to_string(ci_ + 1);

    const bool vib = opts_.channel != Channel::Suction, suc = opts_.channel != Channel::Vibration;
    pos_.vibration_peak = vib ? peak : 0;
    pos_.suction_peak = suc ? peak : 0;
    vl_.ops.clear();
    ramp(vl_, vib, suc, level_, peak, opts_.ramp_up_ms);
    start(vl_);
    stage_ = Stage::RampUp;
}

void Session::after_hold(bool stopped) {
    cres_.reached_peak_after_ms = static_cast<int>(clock_ - cres_.started_at_ms);
    cres_.stopped_by_user = stopped;
    const bool vib = opts_.channel != Channel::Suction, suc = opts_.channel != Channel::Vibration;
    vl_.ops.clear();
    if (stopped) {
        // Interrupt means off now, whatever the technique: no plateau after a request to stop.
        write(vib, suc, 0);
        level_ = 0;
        wait(vl_, rest_ms_);
        start(vl_);
        stage_ = Stage::Rest;
        return;
    }
    target_ = opts_.technique == Technique::Plateau ? clamp01(cycle_peak_ * opts_.plateau_fraction) : 0.0;
    ramp(vl_, vib, suc, cycle_peak_, target_, rest_ms_);
    start(vl_);
    stage_ = Stage::RampDown;
}

void Session::end_cycle() {
    cres_.ended_at_ms = clock_;
    if (on_cycle) on_cycle(cres_);
    if (++ci_ >= opts_.cycles) {
        finish();
    } else {
        stage_ = Stage::Begin;
    }
}

double Session::step_custom(double budget, bool& changed) {
    const bool vib = opts_.channel != Channel::Suction, suc = opts_.channel != Channel::Vibration;
    switch (stage_) {
        case Stage::Begin: begin_cycle(); changed = true; return 0;
        case Stage::RampUp: {
            const double used = run_lane(vl_, budget);
            if (!vl_.done) return used;
            changed = true;
            if (vl_.stopped) {
                after_hold(true);
            } else {
                vl_.ops.clear();
                hold(vl_, cres_.hold_ms);
                start(vl_);
                stage_ = Stage::Hold;
            }
            return used;
        }
        case Stage::Hold: {
            const double used = run_lane(vl_, budget);
            if (!vl_.done) return used;
            changed = true;
            after_hold(vl_.stopped);
            return used;
        }
        case Stage::RampDown: {
            const double used = run_lane(vl_, budget);
            if (!vl_.done) return used;
            changed = true;
            level_ = target_;
            if (vl_.stopped) {  // an Interrupt in the ramp down also means off now and a full rest
                cres_.stopped_by_user = true;
                write(vib, suc, 0);
                level_ = 0;
            }
            if (vl_.stopped || opts_.technique != Technique::Plateau) {
                vl_.ops.clear();
                wait(vl_, rest_ms_);
                start(vl_);
                stage_ = Stage::Rest;
            } else {
                end_cycle();  // plateau: the ramp down was the rest
            }
            return used;
        }
        case Stage::Rest: {
            const double used = run_lane(vl_, budget);
            if (!vl_.done) return used;
            changed = true;
            end_cycle();
            return used;
        }
        default: return 0;
    }
}

}  // namespace samn::training
