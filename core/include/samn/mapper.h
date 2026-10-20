#pragma once
// Funscript -> device-intensity mapper (port of SamNPlayer funscript/mapper.go
// via samn/formats/mapper.py). Not ported: recipe/metadata plumbing
// (MapOptionsFromScript) and the geometry behind SpatialContactIntensity; the
// latter is injectable via MapOptions::spatial_contact (at_ms -> 0..1).
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "samn/funscript.h"

namespace samn {

enum class SyncMode {
    Independent = 0,
    Synchronized = 1,
    Alternating = 2,
    VibrationOnly = 3,
    SuctionOnly = 4,
    SuctionPosition = 5,
};

// Go: SyncMode.String
std::string SyncModeName(SyncMode m);
// Go: ParseSyncMode. "" -> Independent. Throws std::invalid_argument for unknown names.
SyncMode ParseSyncMode(const std::string& s);

struct Frame {
    int64_t at = 0;
    double vibration = 0;
    double suction = 0;
};

// Inclusive window, ms.
struct TrackingGap {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
};

// start inclusive, end exclusive.
struct SceneFeelRange {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string feel;
};

// A phase of the clip for playback (D-157): intro / empty hold the device
// at rest, climax builds up, cool-down fades out. start inclusive, end
// exclusive. Other names (the feels) are left to scene_ranges.
struct PhaseRange {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string phase;
};

inline const char* const ContactCurveLinear = "linear";
inline const char* const ContactCurveSoft = "soft";
inline const char* const ContactCurvePeak = "peak";
inline const char* const ContactCurveImpulse = "impulse";

constexpr double ImpulseContactThreshold = 0.55;
constexpr double SpatialDepthWeight = 0.30;
constexpr double SpatialContactEnvelopeSmooth = 0.18;
constexpr double DefaultContactEnvelopeSmooth = 0.45;
constexpr double DefaultContactVibrationSpan = 0.75;
constexpr double ContactVibrationMinSpan = 5.0;

inline const char* const SceneFeelNone = "";
inline const char* const SceneFeelOral = "oral";
inline const char* const SceneFeelIntercourse = "intercourse";
inline const char* const SceneFeelBreast = "breast";
inline const char* const SceneFeelHand = "hand";

struct MapOptions {
    int64_t tick_ms = 50;
    double max_speed = 0.6;
    double min_vibration = 0.15;
    double min_suction = 0.0;
    double smoothing = 0.3;
    SyncMode sync = SyncMode::Independent;
    bool contact_vibration = false;
    double contact_vibration_span = 0.0;
    std::string contact_vibration_curve;
    double contact_vibration_envelope = 0.0;
    std::vector<TrackingGap> tracking_gaps;
    std::string scene_feel;
    std::vector<SceneFeelRange> scene_ranges;
    std::vector<PhaseRange> phase_ranges;
    bool vibration_follows_suction = false;
    double balance = 0.0;
    bool use_explicit_axes = false;
    std::vector<Action> vibration_axis;
    std::vector<Action> suction_axis;
    // Replacement for Go's marks/trajectory spatial model: at_ms -> 0..1.
    std::function<double(int64_t)> spatial_contact;
};

MapOptions DefaultMapOptions();
std::string NormalizeContactCurve(const std::string& name);
double EffectiveContactSpan(double span);
double Clamp01(double v);
// Scales value into [floor, 1], does not clip.
double LiftFloor(double value, double floor);
double ApplyContactCurve(double t, const std::string& curve);
// NaN -> 0.
double ClampBalance(double b);
// Returns {vib, suc}.
std::pair<double, double> BalanceScales(double balance);
// Returns the suction value, or nullopt for an unknown feel.
std::optional<double> SceneSuction(const std::string& feel, double depth, double intensity);
// Linear interpolation, empty -> 0.
double SampleAxisPos(const std::vector<Action>& actions, int64_t t);
// pos -> 100-pos (clamped) when inverted.
std::vector<Action> PlaybackActions(const std::vector<Action>& actions, bool inverted = false);
// The phases that shape playback (intro, empty, climax, cool-down), sorted,
// without empty or unknown ones.
std::vector<PhaseRange> NormalizePhaseRanges(const std::vector<PhaseRange>& rs);
// Shapes mapped frames by phase (D-157), after the mapping: intro and empty
// fade to 0 and back over kPhaseFadeMs at their edges; the climax lifts the
// output towards full as it goes on; the cool-down fades out to 0. Frames
// outside a phase are not touched.
constexpr int64_t kPhaseFadeMs = 1000;
constexpr double kClimaxLift = 0.35;
void ApplyPhaseShape(std::vector<Frame>& frames, const std::vector<PhaseRange>& phases);
// Returns nullopt (Go nil) for < 2 actions.
std::optional<std::vector<Frame>> ToIntensityCurve(const std::vector<Action>& actions,
                                                   const MapOptions& opts, bool inverted = false);

}  // namespace samn
