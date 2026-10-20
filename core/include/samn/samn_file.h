#pragma once
// The .samn script container (port of SamNPlayer Go package `samn` via
// samn/formats/samn_file.py).
//
// .samn is SamNPlayer's own script file: a JSON document with the general
// stroke curve, optional vibration/suction curves, device recipe, chapters,
// strength presets, ... Files are written byte-identically to the Go app:
// keys in Go struct order, omitempty rules, Go float formatting, HTML-escaped
// strings (< > & U+2028/9), two-space indent, no trailing newline.
//
// Unknown top-level keys of a .samn are dropped on read (Go does not preserve
// them). ExportFunscript keeps foreign keys of an existing target file (raw
// number text kept verbatim, top-level and metadata keys sorted).
//
// Reused from the core: Action (funscript.h), MapOptions / SceneFeelRange /
// TrackingGap (mapper.h), Bookmark / Chapter and the OFS-style mark loaders
// (marks.h). Writes are atomic (temp file in the same folder, fsync, rename).
//
// Naming differences to Python/Go where the core already owns the name:
// Parse/Write belong to funscript.h, so the document functions are
// ParseDocument / LoadDocument / SaveDocument / MarshalDocument, and the typed
// community-funscript view is `FunscriptView` (samn::Script is the lean
// mapper-side script).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"
#include "samn/marks.h"

namespace samn {

// Any error the Go package reports as "samn: ..." (Python: SamnError).
class SamnError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline const char* const SamnKind = "samnplayer.script";
constexpr int64_t SamnCurrentVersion = 1;
inline const char* const PlaybackRecipe = "recipe";
inline const char* const PlaybackAxes = "axes";
inline const char* const SamnExt = ".samn";

inline const char* const ProfileStandard = "standard";
inline const char* const ProfileWeich = "weich";
inline const char* const ProfileTf = "tf";
inline const char* const ProfileTj = "tj";

inline const char* const OMarkerPrimary = "primary";
inline const char* const OMarkerSecondary = "secondary";

// ---------------------------------------------------------------------------
// Data model (field order == Go struct order == JSON key order)
// ---------------------------------------------------------------------------

struct StrengthPreset {
    std::string name;
    double vibration_scale = 0;  // vibrationScale
    double suction_scale = 0;    // suctionScale
    double contact_span = 0;     // contactSpan, omitempty
    std::string contact_curve;   // contactCurve, omitempty
};

struct DeviceRecipe {
    std::string sync;
    double min_suction = 0;
    int64_t tick_ms = 0;
    double max_speed = 0;
    double smoothing = 0;
    bool contact_vibration = false;
    double contact_vibration_span = 0;
    std::string contact_vibration_curve;
    std::string scene_feel;
    bool vibration_follows_suction = false;
    double feel_balance = 0;
    std::vector<SceneFeelRange> scene_ranges;
    // Phases that shape playback (D-157); not baked into axes.
    std::vector<PhaseRange> phase_ranges;
    std::string playback_source;
};

struct OMarker {
    int64_t start_ms = 0;  // startMs
    int64_t end_ms = 0;    // endMs
    std::string kind;
    double intensity = 0;
    // Go: OMarker.Validate. Throws SamnError.
    void Validate() const;
};

struct ContactMarkBox {
    int64_t x = 0, y = 0, w = 0, h = 0;
    std::string cls;  // JSON "class", omitempty
    bool fixed = false;
};

struct ContactMarks {
    std::string tip_class;
    std::optional<ContactMarkBox> tip;
    std::optional<ContactMarkBox> primary;
    std::vector<ContactMarkBox> extras;
    bool drive_stroke = false;
};

struct TrajectoryPoint {
    int64_t at_ms = 0;  // atMs
    double x = 0;
    double y = 0;
};

struct TrajectoryData {
    int64_t width = 0;
    int64_t height = 0;
    std::vector<TrajectoryPoint> tip;
    std::vector<TrajectoryPoint> partner;
};

struct SceneMapVideo {
    int64_t duration_ms = 0;  // durationMs
    int64_t width = 0;
    int64_t height = 0;
    std::string sha256_head;
};

struct SceneMapGrid {
    int64_t cols = 0;
    int64_t rows = 0;
};

struct SceneMapWindow {
    int64_t start_ms = 0;  // startMs
    int64_t end_ms = 0;    // endMs
    double tempo_hz = 0;   // tempoHz
    std::string score_b64;
    int64_t chosen = 0;
    std::string sign_rule;  // signRule
    double tracker_r = 0;   // trackerR
    std::vector<double> box;
    std::vector<std::string> marks;
};

struct SceneMapMarkSample {
    int64_t ms = 0;
    std::vector<int64_t> rect;
};

struct SceneMapMark {
    std::string id;
    std::string kind;
    std::vector<int64_t> rect;
    int64_t from_ms = 0;  // fromMs
    int64_t to_ms = 0;    // toMs
    std::optional<int64_t> at_ms;  // atMs
    std::string author;
    std::string created_at;  // createdAt
    std::string cls;         // JSON "class"
    std::string role;
    double confidence = 0;
    std::optional<bool> reviewed;
    bool follow = false;
    std::vector<SceneMapMarkSample> path;
};

struct SceneMapEvent {
    int64_t at_ms = 0;  // atMs
    std::string kind;
    std::vector<int64_t> from_box;  // fromBox
    std::vector<int64_t> to_box;    // toBox
};

struct SceneMapData {
    int64_t version = 0;
    SceneMapVideo video;
    SceneMapGrid grid;
    std::vector<SceneMapWindow> windows;
    std::vector<SceneMapMark> marks;
    std::vector<SceneMapEvent> events;
};

struct FunscriptView;

// Go: Document (the native .samn script file).
struct Document {
    int64_t version = 0;
    std::string kind;

    std::string creator;
    int64_t duration_ms = 0;  // durationMs
    std::string video_path;   // videoPath
    std::string profile;

    std::string playback_source;  // playbackSource
    DeviceRecipe recipe;

    std::vector<Action> general;
    std::vector<Action> vibration;
    std::vector<Action> suction;

    std::vector<Chapter> chapters;
    std::vector<Bookmark> bookmarks;
    std::vector<OMarker> o_markers;  // oMarkers
    std::vector<TrackingGap> tracking_gaps;  // trackingGaps

    std::vector<StrengthPreset> strength_presets;  // strengthPresets
    std::string active_strength;                   // activeStrength

    std::optional<ContactMarks> contact_marks;  // contactMarks
    std::optional<TrajectoryData> trajectory;
    std::optional<SceneMapData> scene_map;  // sceneMap

    std::optional<double> quality_score;   // qualityScore
    std::optional<bool> quality_passed;    // qualityPassed
    std::vector<std::string> quality_warnings;  // qualityWarnings

    // Go: Document.Normalize. Fills defaults, clamps curves; throws SamnError.
    void Normalize();
    // Go: Document.ToFunscript (in-memory funscript incl. samn_axes). Normalizes.
    FunscriptView ToFunscript();
    // Go: Document.ExportFunscript (community .funscript, atomic). Normalizes.
    void ExportFunscript(const std::filesystem::path& path);
    // Go: Document.BakeNeoAxes (vibration/suction from the recipe mapper). Normalizes.
    void BakeNeoAxes();
    // The mapper options BakeNeoAxes uses (profile + recipe, no smoothing
    // blur); call on a normalized document.
    MapOptions BakeOptions() const;
    // Go: Document.ActivePreset; nullptr when none is selected.
    const StrengthPreset* ActivePreset() const;
    // Go: Document.ApplyContactRecipe / ApplyFeelRecipe.
    void ApplyContactRecipe(bool enabled, double span, const std::string& curve);
    void ApplyFeelRecipe(const std::string& scene_feel, bool follow_suction, double balance);
};

bool operator==(const Document& a, const Document& b);
inline bool operator!=(const Document& a, const Document& b) { return !(a == b); }

// Typed view of a community .funscript (Go: funscript.Script, the parts samn
// uses). ai_opinion / audio_check are not modelled (only preserved on export).
struct SamnAxes {
    std::vector<Action> vibration;
    std::vector<Action> suction;
};

struct ScriptMetadata {
    int64_t duration = 0;
    std::string creator;
    std::optional<double> quality_score;
    std::optional<bool> quality_passed;
    std::vector<std::string> quality_warnings;
    std::string profile;
    std::optional<DeviceRecipe> device_recipe;
    std::optional<SamnAxes> samn_axes;
    std::vector<TrackingGap> tracking_gaps;
    std::optional<TrajectoryData> trajectory;
    std::optional<ContactMarks> contact_marks;
};

struct FunscriptView {
    std::vector<Action> actions;
    bool inverted = false;
    int dropped = 0;
    ScriptMetadata metadata;
    // Go: Script.Duration.
    int64_t Duration() const { return actions.empty() ? 0 : actions.back().at; }
    // Go: Script.PlaybackActions (100-pos when inverted).
    std::vector<Action> PlaybackActions() const;
};

// ---------------------------------------------------------------------------
// Paths (Go: filepath.Ext rules; '/' is the only separator considered)
// ---------------------------------------------------------------------------
bool IsSamnPath(const std::string& path);
std::string CompanionSamnPath(const std::string& path);
std::string CompanionFunscriptPath(const std::string& path);
// The companion .samn when it exists (as a file) beside a .funscript; empty
// and .samn paths come back unchanged.
std::string PreferSamnCompanion(const std::string& path);

// ---------------------------------------------------------------------------
// Normalize / validation
// ---------------------------------------------------------------------------
std::string NormalizePlaybackSource(const std::string& s);
std::string NormalizeProfile(const std::string& name);
// Go: NormalizeSceneFeel. Aliases map to oral/intercourse/breast/hand; anything else gives "".
std::string NormalizeSceneFeel(const std::string& name);
bool IsDistanceProfile(const std::string& name);
// Clamp pos 0-100, reject negative time, sort by time. Throws SamnError.
std::vector<Action> SanitizeActions(const std::vector<Action>& actions);

// ---------------------------------------------------------------------------
// I/O. All throw SamnError.
// ---------------------------------------------------------------------------
// Go: Parse (unmarshal JSON, then Normalize). Unknown keys are dropped.
Document ParseDocument(const std::string& data);
// Go: Load.
Document LoadDocument(const std::filesystem::path& path);
// Go: json.MarshalIndent(d, "", "  ") of an already normalized Document.
std::string MarshalDocument(const Document& d);
// Go: Save (Normalize in place, then write indented JSON atomically).
void SaveDocument(const std::filesystem::path& path, Document& d);

// Go: funscript.Parse / Load (drops corrupt times, clamps pos, sorts, scales seconds).
FunscriptView ParseFunscript(const std::string& data);
FunscriptView LoadFunscript(const std::filesystem::path& path);
// Go: funscript.LoadOMarkers.
std::vector<OMarker> LoadOMarkers(const std::filesystem::path& path);
// Go: funscript.SaveOMarkers. Validates every marker first (the file stays
// untouched on an error), sorts by start, writes metadata.oMarkers and keeps
// every other field; an empty list writes null. Same bytes as 0.7.
void SaveOMarkers(const std::filesystem::path& path, const std::vector<OMarker>& markers);

// The marks of a script for the Player (bookmarks, chapters, O-markers),
// from a .funscript's metadata or a .samn document (0.7 app_ofs.go /
// app_markers.go). Saving writes only the one list and keeps the rest.
struct ScriptMarks {
    std::vector<Bookmark> bookmarks;
    std::vector<Chapter> chapters;
    std::vector<OMarker> o_markers;
};
ScriptMarks LoadScriptMarks(const std::filesystem::path& path);
void SaveScriptBookmarks(const std::filesystem::path& path, const std::vector<Bookmark>& bookmarks);
void SaveScriptChapters(const std::filesystem::path& path, const std::vector<Chapter>& chapters);
void SaveScriptOMarkers(const std::filesystem::path& path, const std::vector<OMarker>& markers);

// ---------------------------------------------------------------------------
// Recipes and conversion
// ---------------------------------------------------------------------------
MapOptions RecipeFor(const std::string& name);
DeviceRecipe RecipeMeta(const std::string& name);
std::string DefaultSyncForProfile(const std::string& profile);
// Go: funscript.MapOptionsFromScript (recipe + explicit axes).
MapOptions MapOptionsFromScript(const FunscriptView& s);

// Go: FromFunscript (community/legacy .funscript -> Document, as played).
Document FromFunscript(const FunscriptView& s, const std::string& video_path = "");
// Go: LoadFunscriptFile.
Document LoadFunscriptFile(const std::filesystem::path& path);

// Go: funscript.BakeAxesFromRecipe -> {vibration, suction}.
std::pair<std::vector<Action>, std::vector<Action>> BakeAxesFromRecipe(
    const std::vector<Action>& actions, MapOptions opts, int64_t max_step_ms = 200);

// Go: DefaultStrengthPresets (soft / normal / strong).
std::vector<StrengthPreset> DefaultStrengthPresets();

}  // namespace samn
