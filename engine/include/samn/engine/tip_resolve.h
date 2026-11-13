#pragma once
// Resolve a tip box for Create / the queue so the user does not have to
// paint one (FunGen2: Create finds the tip). FindTip at several start times,
// and when the top candidate is below kWeakTipScore the rest of that window
// (up to kTipResolveMaxCandidates) are tried, as repair.cpp tries several
// Find tip proposals rather than failing on #1. Across windows, keep the
// strongest usable pick and only early-exit when the score is clearly strong
// (kStrongTipScore). Guest Create defaults to a short per-window budget
// (kCreateTipMaxSeconds) and tries the shown frame first — FunGen2-snappy.
// Fail closed only when no window has usable motion.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/engine/tip_find.h"

namespace samn::engine {

// Below this best candidate score the "rhythm" is compression noise (a still
// clip scores about 0.002, a clear stroke above 1). Shared by Create and the
// queue.
constexpr double kWeakTipScore = 0.05;
// At or above this, ResolveTipForCreate stops searching further windows.
constexpr double kStrongTipScore = 0.5;
// Guest Create / queue: ~8 s of video per window (sample_every 2 → ~4 s of
// Farneback). Manual Find tip may still use TipFindOptions::max_seconds = 45.
constexpr double kCreateTipMaxSeconds = 8;
constexpr int kTipResolveMaxCandidates = 6;

struct TipResolveOptions {
    double max_seconds = kCreateTipMaxSeconds;
    int sample_every = 2;  // passed to FindTip (0.7 default)
    int max_candidates = kTipResolveMaxCandidates;
    int64_t preview_ms = -1;  // shown frame; tried first when >= 0
    std::function<bool()> cancel;
    std::function<void(int percent)> progress;  // 0..99 over the windows
};

struct TipResolveResult {
    cv::Rect box;
    double score = 0;
    int64_t start_ms = 0;     // the window that won
    int candidate_index = 0;  // 0-based in that window's list
    std::string reason;
    std::vector<TipCandidate> candidates;  // that window, strongest first
};

// Guest order: preview (shown frame) first when given, then 0, ~25 %, ~50 %.
// duration_ms <= 0 → {0} (and preview if given). Starts are unique, in that
// order, and inside [0, duration).
std::vector<int64_t> TipResolveStartMs(int64_t duration_ms, int64_t preview_ms = -1);

// Highest score among candidates[0..max_n). nullopt when none is at or
// above kWeakTipScore. #2..#N can win when #1 is weak (unsorted input, or a
// later window's candidate in ResolveTipForCreate).
std::optional<int> BestUsableTipIndex(const std::vector<TipCandidate>& candidates,
                                      int max_n = kTipResolveMaxCandidates);

// nullopt with *error when the video cannot be read, the run was canceled
// ("canceled"), or no window produced a usable candidate. Early-exits when a
// window's best usable score is at or above kStrongTipScore; otherwise keeps
// the strongest usable pick across the remaining windows.
std::optional<TipResolveResult> ResolveTipForCreate(const std::filesystem::path& video,
                                                    const TipResolveOptions& options, std::string* error);

}  // namespace samn::engine
