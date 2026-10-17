#pragma once
// From a script to the frames the device gets at Play: the Player's mapping
// settings over the script's own recipe, the strength preset of a .samn and
// the live runtime adjust. Ports of 0.7 cmd/gui-wails/app_playback.go
// (StartPlayback, the part before the player starts), sam/runtime.go
// (RuntimeAdjust, AdjustDeviceFrames) and the Effects presets of
// frontend/src/player_layout.js (playback.effect_presets).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"
#include "samn/samn_file.h"

namespace samn {

// ---------------------------------------------------------------------------
// Runtime adjust (sam/runtime.go)
// ---------------------------------------------------------------------------

// Live correction on frames that are already mapped; files stay untouched.
// The defaults leave the frames unchanged. Unlike 0.7, a scale of 0 is a
// real value (PS-11): 0.7 turned IntensityScale / SuctionScale 0 into 1.
struct RuntimeAdjust {
    double intensity_scale = 1;  // times vibration, clamped 0-2
    double intensity_boost = 0;  // added after the scale, clamped -1..1
    double suction_scale = 1;    // times suction, clamped 0-2
    double extra_smooth = 0;     // extra EMA on both channels, clamped 0-0.95
    bool mute_contact = false;   // vibration 0
};

// Clamps every field into its range; a value that is not finite takes the
// default.
RuntimeAdjust Normalized(RuntimeAdjust a);
// True when AdjustDeviceFrames would change nothing.
bool IsIdentity(const RuntimeAdjust& a);
// Go: AdjustDeviceFrames. Identity: the frames come back unchanged.
std::vector<Frame> AdjustDeviceFrames(const std::vector<Frame>& frames, const RuntimeAdjust& adj);

// ---------------------------------------------------------------------------
// Player mapping settings and their precedence (StartPlayback)
// ---------------------------------------------------------------------------

// The settings keys (0.7 app_settings.go) and their defaults.
inline const char* const kKeySyncMode = "playback.sync_mode";
inline const char* const kKeyTickMs = "playback.tick_ms";
inline const char* const kKeyMapMaxSpeed = "playback.max_speed";
inline const char* const kKeySmoothing = "playback.smoothing";
inline const char* const kKeyContactIntensity = "playback.contact_intensity";
inline const char* const kKeyEffectPresets = "playback.effect_presets";
constexpr int64_t kDefaultTickMs = 50;
constexpr double kDefaultMapMaxSpeed = 0.6;
constexpr double kDefaultSmoothing = 0.3;
// The Player's ranges (0.7 playback.js sliders).
constexpr int64_t kTickMinMs = 10, kTickMaxMs = 200;
constexpr double kMapMaxSpeedMin = 0.1, kMapMaxSpeedMax = 2.0;
constexpr double kContactIntensityMax = 2.0;
constexpr double kContactSpanMin = 0.4, kContactSpanMax = 0.95;

// What the Player sends with Play (0.7 PlaybackOptions, the mapping part).
struct PlaybackChoices {
    std::string sync_mode = "independent";  // "" counts as independent
    int64_t tick_ms = kDefaultTickMs;       // > 0 replaces the recipe's
    double max_speed = kDefaultMapMaxSpeed; // > 0 replaces the recipe's
    double smoothing = kDefaultSmoothing;   // see PlanPlayback
    bool contact_off = false;               // "Contact vibration off"
    double contact_intensity = 1;           // 0-2; 0 mutes the vibration
    double contact_extra_smooth = 0;        // 0.7 Play always sent 0
    // Live contact knobs; none = the recipe's (0.7 sent 0 / "").
    std::optional<double> contact_span;
    std::optional<std::string> contact_curve;
};

// 0.7 playback.js sends the contact choices only for a script whose recipe
// has contact vibration on; any other script gets intensity 1, contact on
// and the recipe's span and curve.
PlaybackChoices ChoicesForScript(PlaybackChoices c, bool recipeHasContact);

// Everything StartPlayback decides before mapping.
struct PlaybackPlan {
    MapOptions map;              // what the mapper runs with
    bool contact_on = false;     // contact vibration path (recipe mode)
    RuntimeAdjust adjust;        // the live adjust after mapping
    double suction_scale = 1;    // the strength preset's suction scale
};

// Go: StartPlayback's precedence, exactly:
//  - contact knobs (span, curve, off) apply to distance profiles (tf / tj)
//    and to scripts whose recipe has contact vibration on; the contact path
//    runs only in recipe mode (not with explicit axes);
//  - tick and max speed replace the recipe's only when greater than 0;
//  - smoothing replaces the recipe's when greater than 0, and always (0 too)
//    for a plain stroke script in recipe mode; a tf / tj script or an axes
//    script keeps its own smoothing when the setting is 0;
//  - the sync mode applies to non-distance scripts, and to tf / tj only when
//    it is not "independent" (or ""); never in axes mode;
//  - a strength preset multiplies the contact intensity (an intensity of 0
//    counts as 1 there, the mute stays) and the suction, and in recipe mode
//    with contact vibration its span / curve replace the live knobs;
//  - the vibration is muted with "contact off" or an intensity of 0 or less.
// Throws std::invalid_argument for an unknown sync mode (as 0.7).
PlaybackPlan PlanPlayback(const MapOptions& fromScript, const std::string& profile, const PlaybackChoices& c,
                          const StrengthPreset* preset);

// The contact vibration path of 0.7 Play (sam.PlaybackFramesFromFunscript:
// Enrich, Densify, ToDeviceFrames) for a script with contact vibration on
// and the sync "suction from position": per tick the contact intensity from
// the interpolated position (span and curve from `opts`; the curve here
// knows linear / soft / peak, anything else is linear, as 0.7 sam), lifted
// by min_vibration, through the contact envelope (opts
// contact_vibration_envelope: < 0 off, 0 or >= 1 the default 0.45), then the
// smoothing; tracking gaps and positions below the contact window mute the
// vibration (also through the smoothing); suction follows the position.
// nullopt when that path does not apply (0.7 then maps with
// ToIntensityCurve) or for fewer than 2 actions.
std::optional<std::vector<Frame>> ContactDeviceFrames(const std::vector<Action>& actions, const MapOptions& opts);

// Map the actions with the plan (the contact path when plan.contact_on and
// it applies, else ToIntensityCurve), then the runtime adjust and the
// preset's suction scale (clamped 0-1). Empty for fewer than 2 actions.
std::vector<Frame> PlaybackFrames(const std::vector<Action>& actions, const PlaybackPlan& plan);

// ---------------------------------------------------------------------------
// Effects presets (player_layout.js wireEffectsTabsAndPresets)
// ---------------------------------------------------------------------------

// One preset: master intensity 0-100, smoothing, max speed, tick, soft
// start (ms) and Reduce peaks. Fields a stored preset lacks stay unset and
// leave that control as it is when applied (0.7 `key in values`).
struct EffectPreset {
    std::optional<double> intensity;
    std::optional<double> smoothing;
    std::optional<double> max_speed;
    std::optional<double> tick_ms;
    std::optional<double> soft_start_ms;
    std::optional<bool> reduce_peaks;
};

// The values of 0.7 FX_DEFAULTS (the "Default" entry).
EffectPreset DefaultEffectPreset();
// A preset name: trimmed, at most 40 characters (UTF-8 code points); empty
// or the reserved "__default" is invalid (returns "").
std::string CleanPresetName(const std::string& name);
// Reads the settings text (a JSON object name -> values). Anything that is
// not an object gives no presets; entries that are not objects are skipped,
// values that are not numbers / booleans stay unset.
std::map<std::string, EffectPreset> ParseEffectPresets(const std::string& json);
// The settings text: a JSON object, names sorted, unset fields left out.
std::string SerializeEffectPresets(const std::map<std::string, EffectPreset>& presets);

}  // namespace samn
