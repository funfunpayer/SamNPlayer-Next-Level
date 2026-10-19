#pragma once
// Find tip: proposes the tip box from the video's rhythmic motion, so the
// user does not have to mark it by hand (0.7 generator/auto_roi.py, which
// ran as a Python process; here part of the engine, D-140).
//
// The video is read on a coarse grid (12 x 8 cells at 320 px wide), every
// second frame: dense optical flow (Farneback) minus the camera's own motion
// (features + a partial affine fit), the vertical rest per cell over time,
// scored by how strongly one frequency in 0.1-4 Hz dominates (FFT) times the
// motion's size. The strongest compact region wins; the others are kept as
// candidates the user can pick from. A proposal, never applied silently.
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace samn::engine {

struct TipCandidate {
    cv::Rect box;      // video pixels
    double score = 0;  // peak periodicity x compactness
};

struct TipFindResult {
    cv::Rect box;                          // the proposal (slightly inset)
    std::vector<TipCandidate> candidates;  // strongest first
};

struct TipFindOptions {
    double max_seconds = 45;  // 0.7 max_seconds
    int sample_every = 2;     // 0.7 sample_every
    int64_t start_ms = 0;     // look from here (a scene after a cut)
    std::function<bool()> cancel;
    std::function<void(int percent)> progress;
};

// nullopt with *error when the video cannot be read, is too short, has no
// rhythmic motion, or the run was canceled ("canceled"). The error texts are
// 0.7's, in English.
std::optional<TipFindResult> FindTip(const std::filesystem::path& video, const TipFindOptions& options,
                                     std::string* error);

// The parts, for tests and parity checks (0.7 names).
double PeriodicityScore(const std::vector<double>& signal, double fps);
std::vector<std::vector<std::pair<int, int>>> PeakRegions(const cv::Mat& scores, double decay = 0.4,
                                                          int max_regions = 6);

}  // namespace samn::engine
