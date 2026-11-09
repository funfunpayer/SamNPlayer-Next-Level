#include "samn/base_rhythm.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "samn/settings.h"

namespace samn {

namespace {

std::uint64_t SplitMix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Knot value in [-1, 1].
double Knot(std::uint64_t seed, std::int64_t k) {
    const std::uint64_t h = SplitMix64(seed ^ SplitMix64(std::uint64_t(k)));
    return double(h >> 11) / double(1ULL << 52) - 1.0;
}

double NumberOr(const Settings& s, const char* key, double fallback) {
    try {
        const auto v = s.get(key);
        if (v.is_number()) return v.get<double>();
    } catch (const std::exception&) {
        // Unreadable value: the default stays.
    }
    return fallback;
}

}  // namespace

BaseRhythm ClampBaseRhythm(BaseRhythm r) {
    r.bpm = std::isfinite(r.bpm) ? std::clamp(r.bpm, kBaseBpmMin, kBaseBpmMax) : kBaseBpmDefault;
    r.variation_pct = std::isfinite(r.variation_pct) ? std::clamp(r.variation_pct, 0.0, kBaseBpmVariationMax)
                                                     : kBaseBpmVariationDefault;
    return r;
}

BaseRhythm BaseRhythmFromSettings(const Settings& s) {
    BaseRhythm r;
    r.bpm = NumberOr(s, kBaseBpmKey, kBaseBpmDefault);
    r.variation_pct = NumberOr(s, kBaseBpmVariationKey, kBaseBpmVariationDefault);
    return ClampBaseRhythm(r);
}

bool RhythmicFillFromSettings(const Settings& s) {
    try {
        const auto v = s.get(kRhythmicFillKey);
        if (v.is_boolean()) return v.get<bool>();
    } catch (const std::exception&) {
    }
    return false;
}

double BaseRhythmDrift(const BaseRhythm& r, double t_ms) {
    if (!std::isfinite(t_ms)) return 0;
    const double x = t_ms / kBaseRhythmDriftKnotMs;
    const double k = std::floor(x);
    const double f = x - k;
    const double s = f * f * (3 - 2 * f);
    const auto ki = std::int64_t(k);
    return Knot(r.seed, ki) + (Knot(r.seed, ki + 1) - Knot(r.seed, ki)) * s;
}

double BaseBpmAt(const BaseRhythm& r, double t_ms) {
    const BaseRhythm c = ClampBaseRhythm(r);
    return c.bpm * (1 + c.variation_pct / 100.0 * BaseRhythmDrift(c, t_ms));
}

std::vector<double> BaseBeatGrid(const BaseRhythm& r, double start_ms, double end_ms) {
    std::vector<double> out;
    if (!std::isfinite(start_ms) || !std::isfinite(end_ms) || end_ms <= start_ms) return out;
    const BaseRhythm c = ClampBaseRhythm(r);
    const double fastest = c.bpm * (1 + c.variation_pct / 100.0);
    out.reserve(std::size_t((end_ms - start_ms) * fastest / 60000.0) + 2);
    for (double t = start_ms; t < end_ms; t += 60000.0 / BaseBpmAt(c, t)) out.push_back(t);
    return out;
}

}  // namespace samn
