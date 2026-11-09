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

// Scene-cut test that adapts to the clip (self-check, design §3.3): the
// signature difference must also be an outlier against the median of the
// last `window` frames (factor), so fast motion on a small picture is not
// a cut. On the Owner clip (256x144) IsSceneCut fired 1992 times in 570 s;
// this test fires twice. The histogram rule of IsSceneCut is kept as is.
class AdaptiveSceneCut {
public:
    explicit AdaptiveSceneCut(int window = 60, double factor = 3.0) : window_(window), factor_(factor) {}
    bool Test(const cv::Mat& prev_gray, const cv::Mat& gray);

private:
    int window_;
    double factor_;
    std::vector<double> past_;  // the last `window` signature differences, newest last
};

// The camera's motion between two gray frames: a similarity (shift, scale,
// rotation) from background features outside the excluded boxes (10 px
// padding); the same estimate as 0.7, of which 0.7 used only the y shift.
struct CameraMotion {
    // Below this RANSAC inlier share an estimate is not trusted: random
    // matches on a featureless or noisy picture fit a model too (REJ-09:
    // 0.24 on noise against 0.84-0.90 on real background, GD-14).
    static constexpr double kMinInliers = 0.5;
    // RANSAC inlier distance. 0.7 used OpenCV's 3 px; a foreground moving 1
    // to 3 px per frame against the background then joins the background's
    // model, the fit becomes a compromise and the inlier share stays high,
    // so the gate cannot see it (camera test "a large moving foreground":
    // r 0.91 at 3 px, 0.998 at 1 px). Features tracked by LK are sub-pixel.
    static constexpr double kRansacPx = 1.0;
    static constexpr double kRansacPx07 = 3.0;
    // No camera of a stroke video zooms by more than 20 %, turns by more
    // than 0.2 rad or moves its centre by more than a quarter of the
    // picture between two frames: such a fit is a mismatch.
    static constexpr double kMaxZoomStep = 0.2, kMaxTurnStep = 0.2, kMaxShiftShare = 0.25;
    bool ok = false;      // an estimate exists (enough features, RANSAC converged)
    double inliers = 0;   // RANSAC inlier share of the matched features; 0 without an estimate
    cv::Matx23d m{1, 0, 0, 0, 1, 0};  // a pixel (x, y) of the previous frame -> m * (x, y, 1) in this one
    cv::Size frame;       // the picture's size; empty = the shift bound is not checked

    // Finite and within the bounds above.
    bool Plausible() const;
    bool Usable() const { return ok && inliers >= kMinInliers && Plausible(); }
    double Scale() const;
    double Angle() const;  // radians
    // Where the camera moves a scene point that sat at p in the previous
    // frame, as a displacement: m * p - p (exactly 0 for the identity).
    cv::Point2d Shift(const cv::Point2d& p) const;
};
CameraMotion EstimateCameraMotion(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                                  double ransac_px = CameraMotion::kRansacPx);

// Both fits from the same matched features: fine at kRansacPx and, when
// asked for or when the fine fit is not usable, coarse at 0.7's
// kRansacPx07 (coarse.ok false when it was not fitted).
struct CameraFits {
    CameraMotion fine, coarse;
};
CameraFits EstimateCameraFits(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                              bool want_coarse);

// The camera's step at the tip, frame by frame (PR #201 review). A frame
// whose fine fit is not usable must not add 0: that drops its whole step
// for good and a real pan comes back as a drift. So:
// 1. the fine fit when usable;
// 2. else the coarse fit when it is plausible, its share is clearly high
//    (kCoarseMinInliers) and its step agrees with the last measured one
//    within kAgreePx, so a foreground that moves 1 to 3 px per frame
//    against the background cannot take over through it;
// 3. else the last measured step, for carry_frames frames (about 0.5 s),
//    then halved every frame.
class CameraSteps {
public:
    static constexpr double kCoarseMinInliers = 0.7;
    static constexpr double kAgreePx = 1.0;
    enum class Source { kFine, kCoarse, kCarried, kNone };

    explicit CameraSteps(int carry_frames) : carry_frames_(carry_frames) {}
    // One frame without a cut; p is the tip in the previous frame.
    cv::Point2d Step(const CameraFits& fits, const cv::Point2d& p, Source* source = nullptr);
    // A scene cut: the new scene's camera starts from rest.
    void Reset();

private:
    int carry_frames_;
    int carried_ = 0;
    cv::Point2d last_{0, 0};
};

// Frames flagged kLostTracker repeat the tracker's last box: there the tip
// did not move in the scene, whatever the camera did, so the compensated
// position repeats the one before too.
void HoldLostPositions(std::vector<double>& v, const std::vector<uint8_t>& lost);

// The y shift of EstimateCameraMotion with 0.7's 3 px RANSAC distance; 0
// means "no estimate", not "no motion" (0.7 estimate_camera_motion; the tip
// finder uses it, unchanged).
// inlier_fraction (optional) receives the RANSAC inlier share, 0 without an
// estimate (GD-14).
double CameraMotionY(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                     double* inlier_fraction = nullptr);

// Savitzky-Golay smoothing like scipy savgol_filter(mode="interp").
std::vector<double> Savgol(const std::vector<double>& y, int window, int polyorder);

// Subtracts the camera shift, smoothed per scene (savgol 9/2), from one
// coordinate (ys with the camera's y shift, xs with its x shift).
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
    // A box found again after a loss or a cut: the position counts, the jump
    // to it is not a velocity.
    void Reanchor(const cv::Rect& r);
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
