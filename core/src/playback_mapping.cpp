#include "samn/playback_mapping.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace samn {

namespace {

double finiteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }

}  // namespace

// ---------------------------------------------------------------------------
// Runtime adjust
// ---------------------------------------------------------------------------

RuntimeAdjust Normalized(RuntimeAdjust a) {
    a.intensity_scale = std::clamp(finiteOr(a.intensity_scale, 1), 0.0, 2.0);
    a.suction_scale = std::clamp(finiteOr(a.suction_scale, 1), 0.0, 2.0);
    a.extra_smooth = std::clamp(finiteOr(a.extra_smooth, 0), 0.0, 0.95);
    a.intensity_boost = std::clamp(finiteOr(a.intensity_boost, 0), -1.0, 1.0);
    return a;
}

bool IsIdentity(const RuntimeAdjust& in) {
    const RuntimeAdjust a = Normalized(in);
    return !a.mute_contact && a.intensity_scale == 1 && a.intensity_boost == 0 && a.suction_scale == 1 &&
           a.extra_smooth == 0;
}

std::vector<Frame> AdjustDeviceFrames(const std::vector<Frame>& frames, const RuntimeAdjust& in) {
    if (frames.empty()) return frames;
    const RuntimeAdjust a = Normalized(in);
    if (IsIdentity(a)) return frames;
    std::vector<Frame> out(frames.size());
    double prevVib = 0, prevSuc = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        double vib = a.mute_contact ? 0.0 : Clamp01(f.vibration * a.intensity_scale + a.intensity_boost);
        double suc = Clamp01(f.suction * a.suction_scale);
        if (a.extra_smooth > 0 && i > 0) {
            vib = a.extra_smooth * prevVib + (1 - a.extra_smooth) * vib;
            suc = a.extra_smooth * prevSuc + (1 - a.extra_smooth) * suc;
        }
        prevVib = vib;
        prevSuc = suc;
        out[i] = {f.at, vib, suc};
    }
    return out;
}

// ---------------------------------------------------------------------------
// Precedence
// ---------------------------------------------------------------------------

PlaybackChoices ChoicesForScript(PlaybackChoices c, bool recipeHasContact) {
    if (!recipeHasContact) {
        c.contact_off = false;
        c.contact_intensity = 1;
        c.contact_span.reset();
        c.contact_curve.reset();
    }
    return c;
}

PlaybackPlan PlanPlayback(const MapOptions& fromScript, const std::string& profile, const PlaybackChoices& c,
                          const StrengthPreset* preset) {
    PlaybackPlan plan;
    MapOptions& m = plan.map;
    m = fromScript;
    const bool distance = IsDistanceProfile(profile);
    if (distance || m.contact_vibration) {
        if (c.contact_span && *c.contact_span > 0) m.contact_vibration_span = *c.contact_span;
        if (c.contact_curve && !c.contact_curve->empty()) m.contact_vibration_curve = *c.contact_curve;
        if (c.contact_off) m.contact_vibration = false;
        plan.contact_on = !m.use_explicit_axes && m.contact_vibration;
    }
    if (c.tick_ms > 0) m.tick_ms = c.tick_ms;
    if (c.max_speed > 0) m.max_speed = c.max_speed;
    if (c.smoothing > 0 || (!m.use_explicit_axes && !distance)) m.smoothing = c.smoothing;
    const SyncMode sync = ParseSyncMode(c.sync_mode);  // throws for an unknown name
    if (!distance || (!c.sync_mode.empty() && c.sync_mode != "independent")) {
        if (!m.use_explicit_axes) m.sync = sync;
    }
    double vibScale = c.contact_intensity;
    if (preset) {
        if (vibScale <= 0) vibScale = 1;
        vibScale *= preset->vibration_scale;
        plan.suction_scale = preset->suction_scale;
        if (!m.use_explicit_axes && m.contact_vibration) {
            if (preset->contact_span > 0) m.contact_vibration_span = preset->contact_span;
            if (!preset->contact_curve.empty()) m.contact_vibration_curve = preset->contact_curve;
        }
    }
    plan.adjust.intensity_scale = vibScale;
    plan.adjust.extra_smooth = c.contact_extra_smooth;
    plan.adjust.mute_contact = c.contact_off || c.contact_intensity <= 0;
    return plan;
}

namespace {

// sam/enrich.go applyContactCurveLocal: no impulse curve on this path.
double contactCurveLocal(double t, const std::string& curve) {
    t = Clamp01(t);
    const std::string c = NormalizeContactCurve(curve);
    if (c == ContactCurveSoft) return t * t;
    if (c == ContactCurvePeak) return std::sqrt(t);
    return t;
}

struct DenseFrame {
    int64_t at = 0;
    double pos = 0, range = 0, intensity = 0, confidence = 0;
};

}  // namespace

std::optional<std::vector<Frame>> ContactDeviceFrames(const std::vector<Action>& actions, const MapOptions& opts) {
    if (!(opts.contact_vibration && opts.sync == SyncMode::SuctionPosition)) return std::nullopt;
    if (actions.size() < 2) return std::nullopt;
    const int64_t duration = actions.back().at;
    if (duration <= 0) return std::nullopt;
    const int64_t tick = opts.tick_ms > 0 ? opts.tick_ms : 50;
    const auto inGap = [&](int64_t t) {
        for (const auto& g : opts.tracking_gaps)
            if (t >= g.start_ms && t <= g.end_ms) return true;
        return false;
    };
    // Densify: the actions on the tick grid, contact intensity from the
    // interpolated position (window from the actions' own range).
    const double span = EffectiveContactSpan(opts.contact_vibration_span);
    const std::string curve = opts.contact_vibration_curve;
    double posMin = actions.front().pos, posMax = posMin;
    for (const auto& a : actions) {
        posMin = std::min(posMin, double(a.pos));
        posMax = std::max(posMax, double(a.pos));
    }
    const double posSpan = posMax - posMin;
    const bool contactOK = posSpan >= ContactVibrationMinSpan;
    const double windowMin = posMin + span * posSpan;
    std::vector<DenseFrame> dense;
    dense.reserve(size_t(duration / tick) + 1);
    size_t seg = 0;
    for (int64_t t = 0; t <= duration; t += tick) {
        while (seg < actions.size() - 2 && actions[seg + 1].at <= t) ++seg;
        const Action& a = actions[seg];
        const Action& b = actions[seg + 1];
        int64_t dt = b.at - a.at;
        if (dt <= 0) dt = 1;
        const double frac = double(t - a.at) / double(dt);
        const double pos = a.pos + frac * double(b.pos - a.pos);
        DenseFrame d{t, pos, Clamp01(pos / 100.0), 0, 1};
        if (inGap(t)) {
            d.confidence = 0;
        } else if (contactOK && pos >= windowMin) {
            d.intensity = contactCurveLocal(Clamp01((pos - windowMin) / (posMax - windowMin)), curve);
        }
        dense.push_back(d);
    }
    if (dense.size() < 2) return std::nullopt;
    // ToDeviceFrames on the dense script.
    double envelope = opts.contact_vibration_envelope;
    const bool envelopeOn = envelope >= 0;
    if (envelope == 0 || envelope >= 1) envelope = DefaultContactEnvelopeSmooth;
    bool confidenceLive = false;
    double dMin = dense.front().pos, dMax = dMin;
    for (const auto& d : dense) {
        confidenceLive = confidenceLive || d.confidence > 0;
        dMin = std::min(dMin, d.pos);
        dMax = std::max(dMax, d.pos);
    }
    const bool threshOn = dMax - dMin >= ContactVibrationMinSpan;
    const double threshMin = dMin + span * (dMax - dMin);
    const int64_t denseEnd = dense.back().at;
    std::vector<Frame> frames;
    frames.reserve(dense.size());
    double prevVib = 0, prevSuc = 0, prevContact = 0;
    seg = 0;
    for (int64_t t = 0; t <= denseEnd; t += tick) {
        while (seg < dense.size() - 2 && dense[seg + 1].at <= t) ++seg;
        const DenseFrame& a = dense[seg];
        const DenseFrame& b = dense[seg + 1];
        int64_t dt = b.at - a.at;
        if (dt <= 0) dt = 1;
        const double frac = double(t - a.at) / double(dt);
        const double pos = a.pos + frac * (b.pos - a.pos);
        double suc = Clamp01(pos / 100.0);
        if (a.range > 0 || b.range > 0) {
            const double ra = a.range == 0 ? Clamp01(a.pos / 100) : a.range;
            const double rb = b.range == 0 ? Clamp01(b.pos / 100) : b.range;
            suc = ra + frac * (rb - ra);
        }
        const double intens = a.intensity + frac * (b.intensity - a.intensity);
        const double conf = a.confidence + frac * (b.confidence - a.confidence);
        bool muted = inGap(t) || (confidenceLive && conf < 0.5);
        if (threshOn && pos < threshMin) muted = true;
        double vib = 0;
        if (!muted) {
            vib = Clamp01(intens);
            if (vib > 0 && opts.min_vibration > 0) vib = opts.min_vibration + vib * (1 - opts.min_vibration);
            if (envelopeOn) {
                if (!frames.empty()) vib = envelope * prevContact + (1 - envelope) * vib;
                prevContact = vib;
            }
        } else {
            prevContact = 0;
        }
        if (opts.smoothing > 0 && !frames.empty()) {
            vib = opts.smoothing * prevVib + (1 - opts.smoothing) * vib;
            suc = opts.smoothing * prevSuc + (1 - opts.smoothing) * suc;
        }
        if (muted) vib = 0;
        prevVib = vib;
        prevSuc = suc;
        frames.push_back({t, vib, suc});
    }
    return frames;
}

std::vector<Frame> PlaybackFrames(const std::vector<Action>& actions, const PlaybackPlan& plan) {
    std::optional<std::vector<Frame>> contact;
    if (plan.contact_on) contact = ContactDeviceFrames(actions, plan.map);
    auto frames = contact ? std::move(*contact) : ToIntensityCurve(actions, plan.map).value_or(std::vector<Frame>{});
    frames = AdjustDeviceFrames(frames, plan.adjust);
    if (plan.suction_scale != 1.0)
        for (auto& f : frames) f.suction = Clamp01(f.suction * plan.suction_scale);
    return frames;
}

// ---------------------------------------------------------------------------
// Effects presets
// ---------------------------------------------------------------------------

EffectPreset DefaultEffectPreset() {
    EffectPreset p;
    p.intensity = 100;
    p.smoothing = kDefaultSmoothing;
    p.max_speed = kDefaultMapMaxSpeed;
    p.tick_ms = double(kDefaultTickMs);
    p.soft_start_ms = 500;
    p.reduce_peaks = false;
    return p;
}

std::string CleanPresetName(const std::string& name) {
    const auto isSpace = [](unsigned char ch) { return ch == ' ' || (ch >= '\t' && ch <= '\r'); };
    size_t b = 0, e = name.size();
    while (b < e && isSpace(static_cast<unsigned char>(name[b]))) ++b;
    while (e > b && isSpace(static_cast<unsigned char>(name[e - 1]))) --e;
    std::string out;
    int points = 0;
    for (size_t i = b; i < e; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if ((ch & 0xC0) != 0x80) {  // a code point starts here
            if (points == 40) break;
            ++points;
        }
        out.push_back(static_cast<char>(ch));
    }
    // Cutting at 40 may leave trailing spaces (0.7 trims before slicing).
    if (out.empty() || out == "__default") return {};
    return out;
}

namespace {

std::optional<double> number(const nlohmann::json& o, const char* key) {
    const auto it = o.find(key);
    if (it == o.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
}

}  // namespace

std::map<std::string, EffectPreset> ParseEffectPresets(const std::string& text) {
    std::map<std::string, EffectPreset> out;
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return out;
    for (const auto& [name, v] : j.items()) {
        if (!v.is_object()) continue;
        EffectPreset p;
        p.intensity = number(v, "intensity");
        p.smoothing = number(v, "smoothing");
        p.max_speed = number(v, "maxSpeed");
        p.tick_ms = number(v, "tickMs");
        p.soft_start_ms = number(v, "softStartMs");
        if (const auto it = v.find("reducePeaks"); it != v.end() && it->is_boolean()) p.reduce_peaks = it->get<bool>();
        out[name] = p;
    }
    return out;
}

namespace {

// Whole numbers as integers, as 0.7's JSON.stringify writes them.
nlohmann::json jsNumber(double v) {
    if (std::floor(v) == v && std::fabs(v) < 1e15) return static_cast<int64_t>(v);
    return v;
}

}  // namespace

std::string SerializeEffectPresets(const std::map<std::string, EffectPreset>& presets) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [name, p] : presets) {
        nlohmann::json o = nlohmann::json::object();
        if (p.intensity) o["intensity"] = jsNumber(*p.intensity);
        if (p.smoothing) o["smoothing"] = jsNumber(*p.smoothing);
        if (p.max_speed) o["maxSpeed"] = jsNumber(*p.max_speed);
        if (p.tick_ms) o["tickMs"] = jsNumber(*p.tick_ms);
        if (p.soft_start_ms) o["softStartMs"] = jsNumber(*p.soft_start_ms);
        if (p.reduce_peaks) o["reducePeaks"] = *p.reduce_peaks;
        j[name] = o;
    }
    return j.dump();
}

}  // namespace samn
