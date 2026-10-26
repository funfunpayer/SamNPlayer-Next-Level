#pragma once

// The comparison controller (P3.8, the Owner's "Vergleich Controller"):
// runs SamNPlayer 2's engine on clips and scores it against a reference
// script and against 0.7's output for the same clip, with the rules learned
// from 0.7's benchmark (LESSONS GD-04, GD-05, GD-06): one absolute timeline,
// +-1000 ms lag window, windowed r, flags for boundary-pinned lags, low
// confidence and aliasing, the sample count of every pair, and a manifest
// with content hashes. A default is chosen only from these numbers.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include "samn/engine/signal_chain.h"
#include "samn/motion_fidelity.h"

namespace samn::engine {

struct CompareClip {
    std::string name;
    std::filesystem::path video;
    cv::Rect box;
    int64_t start_ms = 0;                       // the box is drawn here (0.7 startTimeSec)
    SignalOptions signal;                       // profile and overrides
    std::optional<std::filesystem::path> reference;  // human or other trusted script
    std::optional<std::filesystem::path> old_output;  // 0.7's script for the same clip and box
};

struct CompareSettings {
    int max_lag_ms = kDefaultMaxLagMs;
    int lag_step_ms = kDefaultLagStepMs;
    int resample_step_ms = kDefaultResampleStepMs;
    int window_ms = 10000;
};

// One script against another.
struct PairScore {
    std::optional<LagCorrelation> whole;
    bool boundary_pinned = false;    // best lag at the edge of the window: untrusted
    std::vector<WindowResult> windows;
    std::optional<double> windowed_r_median;  // over windows with a correlation
    int windows_scored = 0, windows_low_confidence = 0, windows_aliasing = 0, windows_pinned = 0;
};

PairScore ScorePair(const std::vector<Action>& reference, const std::vector<Action>& candidate,
                    const CompareSettings& s);

struct ClipReport {
    CompareClip clip;
    std::string error;  // non-empty: the clip could not be scored
    std::string video_sha256, reference_sha256, old_sha256;
    int frames = 0, lost_frames = 0;
    std::vector<Action> new_actions;
    std::optional<PairScore> new_vs_reference, old_vs_reference, new_vs_old;
    // GD-05: compared runs must cover the same span; set when the sample
    // counts of new and old against the reference differ.
    bool n_mismatch = false;
};

// Tracks the clip with the engine and scores everything present. cancel is
// checked every frame.
ClipReport RunClip(const CompareClip& clip, const CompareSettings& s, const std::function<bool()>& cancel = {});

// Manifest: {"clips": [{"name", "video", "box": [x,y,w,h], "start_ms", "profile",
// "reference", "old"}], "window_ms", "max_lag_ms"}; relative paths are
// relative to the manifest. nullopt with *error on a bad manifest.
std::optional<std::vector<CompareClip>> LoadManifest(const std::filesystem::path& path, CompareSettings* settings,
                                                     std::string* error);

nlohmann::json ReportJson(const std::vector<ClipReport>& reports, const CompareSettings& s);
// Numbers only, no media and no file names beyond the clip name.
std::string ReportMarkdown(const std::vector<ClipReport>& reports, const CompareSettings& s);

}  // namespace samn::engine
