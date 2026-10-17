#pragma once

// Building blocks of TrackBox, each a port of 0.7's trackcv with the same
// constants. Internal to samn_engine; tested in tests/engine.

#include <cstdint>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

namespace samn::engine {

// Scene cut between two gray frames (0.7 detect_scene_cut): histogram
// correlation below 0.5 or mean signature difference above 12.
double HistCorrelation(const cv::Mat& a, const cv::Mat& b);
double SignatureDiff(const cv::Mat& a, const cv::Mat& b);
bool IsSceneCut(const cv::Mat& prev_gray, const cv::Mat& gray);

// Vertical camera shift between two gray frames from background features
// outside the excluded boxes (10 px padding). 0 means "no reliable
// estimate", not "no motion".
double CameraMotionY(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes);

// Savitzky-Golay smoothing like scipy savgol_filter(mode="interp").
std::vector<double> Savgol(const std::vector<double>& y, int window, int polyorder);

// Subtracts the camera shift, smoothed per scene (savgol 9/2), from ys.
void ApplyCameraCompensation(std::vector<double>& ys, std::vector<double> camera_dy, const std::vector<int>& cuts);

// Flags a single-frame jump far outside the recent accepted motion.
class DispGuard {
public:
    bool Implausible(double d) const;
    void Accept(double d);

private:
    std::vector<double> recent_;
};

// Keeps a lost box alive for a few frames along its last velocity.
class Coast {
public:
    static constexpr int kDefaultFrames = 8;
    void ObserveOk(const cv::Rect& r);
    // The coasted box, or nullopt when the budget is spent.
    std::optional<cv::Rect> OnLost();

private:
    int lost_ = 0;
    bool have_pos_ = false, have_vel_ = false;
    double prev_cx_ = 0, prev_cy_ = 0, vx_ = 0, vy_ = 0;
    cv::Rect box_;
};

// Remembers crops of the tracked region and finds them again after a loss
// or a cut; a candidate must still resemble the first crop (fail closed).
class AppearanceMemory {
public:
    static constexpr double kMinScore = 0.55;
    void Remember(const cv::Mat& gray, const cv::Rect& box);
    // Similarity of the region around box to the first crop; nullopt when
    // there is nothing to compare yet.
    std::optional<double> MatchesOriginal(const cv::Mat& gray, const cv::Rect& box) const;
    std::optional<cv::Rect> Reacquire(const cv::Mat& gray) const;
    size_t size() const { return templates_.size(); }

private:
    static constexpr size_t kMaxTemplates = 8;
    static constexpr double kDownscale = 0.5;
    std::vector<cv::Mat> templates_;
};

// Frames a run can process: the container's count minus the frames skipped
// by the seek, capped by max_frames; 0 when unknown (0.7 progressTotalFrames).
int ProgressTotal(int64_t frame_count, int64_t first_index, int max_frames);

// 0.7 trackcv.ExtractTemplate: crop clamped at the top-left, scaled; empty
// when too small.
cv::Mat ExtractTemplate(const cv::Mat& gray, cv::Rect box, double downscale);

}  // namespace samn::engine
