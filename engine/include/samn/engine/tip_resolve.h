#pragma once
// Resolve a tip box for Create / the queue so the user does not have to
// paint one (FunGen2: Create finds the tip). FindTip at several start times,
// and when the top candidate is below kWeakTipScore the rest of that window
// (up to kTipResolveMaxCandidates) are tried, as repair.cpp tries several
// Find tip proposals rather than failing on #1. Fail closed only when no
// window has usable motion.
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
constexpr int kTipResolveMaxCandidates = 6;

struct TipResolveOptions {
    double max_seconds = 45;  // per window, same default as FindTip
    int max_candidates = kTipResolveMaxCandidates;
    int64_t preview_ms = -1;  // extra start (the shown frame); < 0 omitted
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

// 0, ~10 %, ~25 %, ~50 % of duration, then preview_ms if it is new.
// duration_ms <= 0 → {0} (and preview if given). Starts are unique, in that
// order, and inside [0, duration).
std::vector<int64_t> TipResolveStartMs(int64_t duration_ms, int64_t preview_ms = -1);

// Highest score among candidates[0..max_n). nullopt when none is at or
// above kWeakTipScore. #2..#N can win when #1 is weak (unsorted input, or a
// later window's candidate in ResolveTipForCreate).
std::optional<int> BestUsableTipIndex(const std::vector<TipCandidate>& candidates,
                                      int max_n = kTipResolveMaxCandidates);

// nullopt with *error when the video cannot be read, the run was canceled
// ("canceled"), or no window produced a usable candidate. Early-exits on the
// first usable pick so Create stays as snappy as a single FindTip when t=0
// already has motion.
std::optional<TipResolveResult> ResolveTipForCreate(const std::filesystem::path& video,
                                                    const TipResolveOptions& options, std::string* error);

}  // namespace samn::engine
