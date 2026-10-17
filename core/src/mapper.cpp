#include "samn/mapper.h"

#include <algorithm>
#include <cmath>

namespace samn {

namespace {

const char* const kSyncNames[] = {"independent",   "synchronized", "alternating",
                                  "vibration_only", "suction_only", "suction_position"};

struct SceneShape {
    const char* feel;
    double floor, range, depth_w, speed_w;
};
const SceneShape kSceneShapes[] = {
    {SceneFeelOral, 0.30, 0.70, 0.65, 0.35},
    {SceneFeelIntercourse, 0.15, 0.85, 0.45, 0.55},
    {SceneFeelBreast, 0.20, 0.55, 0.70, 0.30},
    {SceneFeelHand, 0.10, 0.70, 0.25, 0.75},
};

std::string StripLower(const std::string& s) {
    size_t b = 0, e = s.size();
    auto ws = [](unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f); };
    while (b < e && ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && ws(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string r = s.substr(b, e - b);
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::string SceneFeelAt(const MapOptions& opts, int64_t t) {
    for (const auto& r : opts.scene_ranges)
        if (r.start_ms <= t && t < r.end_ms) return r.feel;
    return opts.scene_feel;
}

}  // namespace

std::string SyncModeName(SyncMode m) { return kSyncNames[static_cast<int>(m)]; }

SyncMode ParseSyncMode(const std::string& s) {
    if (s.empty()) return SyncMode::Independent;
    for (int i = 0; i < 6; ++i)
        if (s == kSyncNames[i]) return static_cast<SyncMode>(i);
    throw std::invalid_argument("funscript: unknown sync mode '" + s + "'");
}

MapOptions DefaultMapOptions() {
    MapOptions o;
    o.tick_ms = 50;
    o.max_speed = 0.6;
    o.min_vibration = 0.15;
    o.min_suction = 0.0;
    o.smoothing = 0.3;
    o.sync = SyncMode::Independent;
    return o;
}

std::string NormalizeContactCurve(const std::string& name) {
    std::string n = StripLower(name);
    if (n == ContactCurveSoft || n == "soft_entry" || n == "weicher") return ContactCurveSoft;
    if (n == ContactCurvePeak || n == "strong_peak" || n == "peaky") return ContactCurvePeak;
    if (n == ContactCurveImpulse || n == "events" || n == "contact_events" || n == "peak_emphasis")
        return ContactCurveImpulse;
    return ContactCurveLinear;
}

double EffectiveContactSpan(double span) {
    if (span <= 0 || span >= 1) return DefaultContactVibrationSpan;
    if (span < 0.4) return 0.4;
    if (span > 0.95) return 0.95;
    return span;
}

double Clamp01(double v) {
    if (v < 0) return 0.0;
    if (v > 1) return 1.0;
    return v;
}

double LiftFloor(double value, double floor) {
    if (floor <= 0) return value;
    if (floor >= 1) return 1.0;
    return floor + value * (1 - floor);
}

double ApplyContactCurve(double t, const std::string& curve) {
    t = Clamp01(t);
    std::string c = NormalizeContactCurve(curve);
    if (c == ContactCurveSoft) return t * t;
    if (c == ContactCurvePeak) return std::sqrt(t);
    if (c == ContactCurveImpulse) {
        if (t < ImpulseContactThreshold) return 0.0;
        double u = (t - ImpulseContactThreshold) / (1 - ImpulseContactThreshold);
        return u * u * u;
    }
    return t;
}

double ClampBalance(double b) {
    if (b != b) return 0.0;
    return std::max(-1.0, std::min(1.0, b));
}

std::pair<double, double> BalanceScales(double balance) {
    balance = ClampBalance(balance);
    double vib = 1.0, suc = 1.0;
    if (balance > 0)
        vib = 1 - balance;
    else if (balance < 0)
        suc = 1 + balance;
    return {vib, suc};
}

std::optional<double> SceneSuction(const std::string& feel, double depth, double intensity) {
    for (const auto& s : kSceneShapes) {
        if (feel == s.feel)
            return Clamp01(s.floor + s.range * (s.depth_w * Clamp01(depth) + s.speed_w * Clamp01(intensity)));
    }
    return std::nullopt;
}

double SampleAxisPos(const std::vector<Action>& actions, int64_t t) {
    if (actions.empty()) return 0.0;
    if (t <= actions.front().at) return static_cast<double>(actions.front().pos);
    const Action& last = actions.back();
    if (t >= last.at) return static_cast<double>(last.pos);
    size_t lo = 0, hi = actions.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (actions[mid].at <= t)
            lo = mid;
        else
            hi = mid;
    }
    const Action& a = actions[lo];
    const Action& b = actions[hi];
    int64_t dt = b.at - a.at;
    if (dt <= 0) return static_cast<double>(a.pos);
    double frac = static_cast<double>(t - a.at) / static_cast<double>(dt);
    return static_cast<double>(a.pos) + frac * static_cast<double>(b.pos - a.pos);
}

std::vector<Action> PlaybackActions(const std::vector<Action>& actions, bool inverted) {
    if (!inverted || actions.empty()) return actions;
    std::vector<Action> out;
    out.reserve(actions.size());
    for (const auto& a : actions) out.push_back({a.at, std::max(0, std::min(100, 100 - a.pos))});
    return out;
}

std::optional<std::vector<Frame>> ToIntensityCurve(const std::vector<Action>& actions,
                                                   const MapOptions& opts, bool inverted) {
    const std::vector<Action> acts = PlaybackActions(actions, inverted);
    if (acts.size() < 2) return std::nullopt;
    const int64_t tick_ms = opts.tick_ms > 0 ? opts.tick_ms : 50;
    const double max_speed = opts.max_speed > 0 ? opts.max_speed : 0.6;
    const int64_t duration = actions.back().at;  // Script.Duration uses the stored actions
    std::vector<Frame> frames;
    size_t seg_idx = 0;
    double prev_vib = 0.0, prev_suc = 0.0;
    double prev_contact_vib = 0.0;
    double envelope = opts.contact_vibration_envelope;
    bool envelope_on = true;
    if (envelope < 0)
        envelope_on = false;
    else if (envelope == 0 || envelope >= 1)
        envelope = DefaultContactEnvelopeSmooth;

    double contact_min = 0.0, contact_max = 0.0;
    bool contact_enabled = opts.contact_vibration;
    const double contact_span = EffectiveContactSpan(opts.contact_vibration_span);
    const std::string contact_curve = NormalizeContactCurve(opts.contact_vibration_curve);
    const auto& gaps = opts.tracking_gaps;
    if (contact_enabled) {
        double pos_min = static_cast<double>(acts[0].pos), pos_max = pos_min;
        for (const auto& a : acts) {
            pos_min = std::min(pos_min, static_cast<double>(a.pos));
            pos_max = std::max(pos_max, static_cast<double>(a.pos));
        }
        if (pos_max - pos_min < ContactVibrationMinSpan) {
            contact_enabled = false;
        } else {
            contact_min = pos_min + contact_span * (pos_max - pos_min);
            contact_max = pos_max;
        }
    }

    auto in_gap = [&](int64_t t) {
        for (const auto& g : gaps)
            if (g.start_ms <= t && t <= g.end_ms) return true;
        return false;
    };

    const bool spatial_on = opts.contact_vibration && static_cast<bool>(opts.spatial_contact);
    bool last_spatial_hit = false;

    auto contact_vib_at = [&](double pos, int64_t t) {
        last_spatial_hit = false;
        if ((!contact_enabled && !spatial_on) || in_gap(t)) return 0.0;
        double depth = 0.0;
        if (contact_enabled && pos >= contact_min) {
            double linear = Clamp01((pos - contact_min) / (contact_max - contact_min));
            depth = ApplyContactCurve(linear, contact_curve);
        }
        double spatial = 0.0;
        if (spatial_on) spatial = ApplyContactCurve(opts.spatial_contact(t), contact_curve);
        double vib = depth;
        if (spatial_on) {
            double weighted = depth * SpatialDepthWeight;
            vib = weighted;
            if (spatial > vib) vib = spatial;
            last_spatial_hit = spatial > 0 && spatial >= weighted;
        }
        if (vib > 0 && opts.min_vibration > 0) vib = LiftFloor(vib, opts.min_vibration);
        return vib;
    };

    auto smooth_contact = [&](double vib, int64_t t) {
        if ((!contact_enabled && !spatial_on) || !envelope_on || in_gap(t)) {
            if (in_gap(t)) prev_contact_vib = 0.0;
            return vib;
        }
        double env = envelope;
        if (last_spatial_hit && SpatialContactEnvelopeSmooth < env) env = SpatialContactEnvelopeSmooth;
        if (!frames.empty()) vib = env * prev_contact_vib + (1 - env) * vib;
        prev_contact_vib = vib;
        return vib;
    };

    const SyncMode sync = opts.sync;
    for (int64_t t = 0; t <= duration; t += tick_ms) {
        double vib, suc;
        if (opts.use_explicit_axes) {
            vib = Clamp01(SampleAxisPos(opts.vibration_axis, t) / 100.0);
            suc = Clamp01(SampleAxisPos(opts.suction_axis, t) / 100.0);
            if (in_gap(t)) vib = 0.0;
        } else {
            while (seg_idx < acts.size() - 2 && acts[seg_idx + 1].at <= t) ++seg_idx;
            const Action& a = acts[seg_idx];
            const Action& b = acts[seg_idx + 1];
            int64_t dt = b.at - a.at;
            if (dt <= 0) dt = 1;
            double speed = std::fabs(static_cast<double>(b.pos - a.pos)) / static_cast<double>(dt);
            double intensity = Clamp01(speed / max_speed);
            double frac = static_cast<double>(t - a.at) / static_cast<double>(dt);
            double pos = static_cast<double>(a.pos) + frac * static_cast<double>(b.pos - a.pos);
            double pos_signal = Clamp01(pos / 100.0);
            switch (sync) {
                case SyncMode::Synchronized:
                    vib = intensity;
                    suc = intensity;
                    break;
                case SyncMode::Alternating:
                    vib = intensity;
                    suc = Clamp01(1 - intensity);
                    break;
                case SyncMode::VibrationOnly:
                    vib = intensity;
                    suc = 0.0;
                    break;
                case SyncMode::SuctionOnly:
                    vib = 0.0;
                    suc = intensity;
                    break;
                case SyncMode::SuctionPosition:
                    vib = 0.0;
                    suc = pos_signal;
                    if (contact_enabled || spatial_on) {
                        vib = contact_vib_at(pos, t);
                        vib = smooth_contact(vib, t);
                    } else if (in_gap(t)) {
                        vib = 0.0;
                    }
                    break;
                default: {
                    vib = intensity;
                    suc = pos_signal;
                    if (auto ss = SceneSuction(SceneFeelAt(opts, t), pos_signal, intensity)) suc = *ss;
                    if (contact_enabled || spatial_on) {
                        double cv = contact_vib_at(pos, t);
                        if (cv > 0) {
                            vib = smooth_contact(cv, t);
                        } else if (in_gap(t)) {
                            vib = 0.0;
                            prev_contact_vib = 0.0;
                        } else {
                            prev_contact_vib = 0.0;
                        }
                    }
                    break;
                }
            }
            if (sync != SyncMode::SuctionOnly && sync != SyncMode::SuctionPosition)
                vib = LiftFloor(vib, opts.min_vibration);
            if (sync != SyncMode::VibrationOnly && sync != SyncMode::SuctionPosition)
                suc = LiftFloor(suc, opts.min_suction);
            if (opts.vibration_follows_suction && sync != SyncMode::SuctionOnly &&
                sync != SyncMode::VibrationOnly)
                vib = suc;
            if (opts.balance != 0) {
                auto [vs, ss2] = BalanceScales(opts.balance);
                vib *= vs;
                suc *= ss2;
            }
        }
        if (opts.smoothing > 0 && !frames.empty()) {
            vib = opts.smoothing * prev_vib + (1 - opts.smoothing) * vib;
            suc = opts.smoothing * prev_suc + (1 - opts.smoothing) * suc;
        }
        if (in_gap(t) && (sync == SyncMode::SuctionPosition || opts.use_explicit_axes ||
                          opts.contact_vibration))
            vib = 0.0;
        prev_vib = vib;
        prev_suc = suc;
        frames.push_back({t, vib, suc});
    }
    return frames;
}

}  // namespace samn
