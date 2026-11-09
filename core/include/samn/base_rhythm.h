#pragma once

// The heart rhythm: the base tempo where the Generator has no beat source
// (D-168). About a resting heart (72 BPM by default, 40-140), slightly
// variable: the tempo drifts slowly (smooth value noise with a knot every
// 40 beats, at most the variation either way), never jitters from beat to
// beat: over the whole clamped range (40-140 BPM, 0-10 %) two adjacent beat
// intervals differ by under 1 % (at most 3 x v / (40 (1 - v)^2), 0.93 % at
// v = 10 %; 0.24 % at the default 3 %), so strokes on this grid stay even. Deterministic: the same settings and
// seed give the same beats on every machine.
//
// Settings (samn/settings.h):
//   generator.base_bpm            default 72, clamped to 40-140
//   generator.base_bpm_variation  percent, default 3, clamped to 0-10
//   generator.rhythmic_fill       default false: gap fill on this grid
//                                 instead of straight lines (samn/improve.h)

#include <cstddef>
#include <cstdint>
#include <vector>

namespace samn {

class Settings;

constexpr double kBaseBpmDefault = 72;
constexpr double kBaseBpmMin = 40;
constexpr double kBaseBpmMax = 140;
constexpr double kBaseBpmVariationDefault = 3;  // percent
constexpr double kBaseBpmVariationMax = 10;     // percent
// Spacing of the drift's knots in beats (33 s at 72 BPM): the tempo changes
// over many beats, never from one to the next.
constexpr double kBaseRhythmDriftKnotBeats = 40;
// BaseBeatGrid returns at most this many beats (8 MB; 240 hours at 72 BPM).
constexpr std::size_t kBaseBeatGridMaxBeats = std::size_t(1) << 20;
constexpr std::uint64_t kBaseRhythmSeed = 0x5EED72B9A11CE5ULL;

constexpr const char* kBaseBpmKey = "generator.base_bpm";
constexpr const char* kBaseBpmVariationKey = "generator.base_bpm_variation";
constexpr const char* kRhythmicFillKey = "generator.rhythmic_fill";

struct BaseRhythm {
    double bpm = kBaseBpmDefault;
    double variation_pct = kBaseBpmVariationDefault;
    std::uint64_t seed = kBaseRhythmSeed;
};

// Clamps bpm to 40-140 and the variation to 0-10 %; NaN takes the default.
BaseRhythm ClampBaseRhythm(BaseRhythm r);

// Reads generator.base_bpm and generator.base_bpm_variation (numbers;
// anything else keeps the default), clamped.
BaseRhythm BaseRhythmFromSettings(const Settings& s);
// generator.rhythmic_fill (default false).
bool RhythmicFillFromSettings(const Settings& s);

// Smooth drift at absolute time t_ms, in [-1, 1] (C1: smoothstep between
// seeded knots kBaseRhythmDriftKnotBeats beats of the clamped bpm apart).
// Times beyond +-1e15 ms (31 700 years) are clamped.
double BaseRhythmDrift(const BaseRhythm& r, double t_ms);
// Tempo at t_ms: bpm x (1 + variation x drift), the bpm clamped first.
double BaseBpmAt(const BaseRhythm& r, double t_ms);

// Beats from start_ms (the first beat) while before end_ms, each interval
// 60000 / BaseBpmAt(its start), at most kBaseBeatGridMaxBeats. Empty when
// end_ms <= start_ms or the range is not finite.
std::vector<double> BaseBeatGrid(const BaseRhythm& r, double start_ms, double end_ms);

}  // namespace samn
