#pragma once
// Script polish after Create and for "Optimize for" (0.7/0.8 funscript/improve.go,
// the FunGen-like end step): trim the ends, heal known tracking-loss windows,
// fill long gaps with straight lines. Mutators only: nothing is invented from
// the sound; an audio tempo only sets the spacing of inserted points.
#include <cstdint>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"

namespace samn {

constexpr int64_t kDefaultFillGapMs = 800;   // fill gaps longer than this (auto mode cap)
constexpr int64_t kDefaultFillStepMs = 100;  // spacing of inserted points

struct ImproveOptions {
    // Trim to [start_ms, end_ms]; 0 (or negative end) = no bound on that side.
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    bool fill_gaps = false;
    // Fill only gaps longer than this; 0 = auto (see AutoFillGapThreshold) plus
    // a second pass for medium holes.
    int64_t max_gap_ms = 0;
    // Spacing of inserted points; 0 = 100 ms, or half the audio period.
    int64_t step_ms = 0;
    double audio_hz = 0;
    // Strip the interior of known tracker-loss windows and bridge them.
    bool heal_tracking_gaps = false;
    std::vector<TrackingGap> tracking_gaps;
};

struct ImproveResult {
    std::vector<Action> actions;
    int before_count = 0;
    int after_count = 0;
    bool trimmed = false;
    int gaps_filled = 0;
    int points_added = 0;
    int64_t fill_gap_ms = 0;
    int64_t fill_step_ms = 0;
    int windows_healed = 0;
    // The healed windows can be dropped from the metadata.
    bool clear_tracking_gaps = false;
};

// Keeps [start_ms, end_ms] (a negative bound = open), interpolating the ends.
// Throws std::invalid_argument with fewer than 2 points before or after.
std::vector<Action> TrimActions(std::vector<Action> actions, int64_t start_ms, int64_t end_ms);

// Linear points across gaps longer than max_gap_ms (<= 0: auto threshold).
std::vector<Action> FillGaps(std::vector<Action> actions, int64_t max_gap_ms, int64_t step_ms, double audio_hz,
                             int* gaps_filled = nullptr, int* points_added = nullptr);

// Removes points strictly inside the windows (the boundaries stay as anchors)
// and bridges only those windows. Refuses (returns the input) when fewer than
// two points would be left.
std::vector<Action> HealTrackingGaps(std::vector<Action> actions, const std::vector<TrackingGap>& gaps, int64_t step_ms,
                                     double audio_hz, int* windows_healed = nullptr, int* points_added = nullptr);

// Trim → heal → fill. Throws std::invalid_argument with fewer than 2 points.
ImproveResult ImproveScript(const std::vector<Action>& actions, const ImproveOptions& opts);

// min(max(median spacing × 8, 2000), 800) — the Script Doctor's gap rule,
// capped so shorter tracker holes are caught too.
int64_t AutoFillGapThreshold(const std::vector<Action>& sorted);

}  // namespace samn
