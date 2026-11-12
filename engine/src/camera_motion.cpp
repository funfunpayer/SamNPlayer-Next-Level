#include <algorithm>
#include <cmath>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include "samn/engine/post_track.h"
#include "track_parts.h"

namespace samn::engine {

namespace {

// Background features of prev_gray outside the excluded boxes and where LK
// finds them in gray; false when fewer than 10 on either side.
bool MatchedFeatures(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                     std::vector<cv::Point2f>* good_prev, std::vector<cv::Point2f>* good_curr) {
    const int h = prev_gray.rows, w = prev_gray.cols, pad = 10;
    cv::Mat mask(h, w, CV_8U, cv::Scalar(255));
    for (const cv::Rect& r : excludes) {
        const int x0 = std::max(0, r.x - pad), y0 = std::max(0, r.y - pad);
        const int x1 = std::min(w, r.x + r.width + pad), y1 = std::min(h, r.y + r.height + pad);
        if (x1 > x0 && y1 > y0) mask(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(0);
    }
    std::vector<cv::Point2f> prev_pts;
    cv::goodFeaturesToTrack(prev_gray, prev_pts, 200, 0.01, 20, mask, 7);
    if (prev_pts.size() < 10) return false;

    std::vector<cv::Point2f> curr_pts;
    std::vector<uchar> status;
    std::vector<float> err;
    cv::calcOpticalFlowPyrLK(prev_gray, gray, prev_pts, curr_pts, status, err);
    for (size_t i = 0; i < status.size(); ++i) {
        if (!status[i]) continue;
        good_prev->push_back(prev_pts[i]);
        good_curr->push_back(curr_pts[i]);
    }
    return good_prev->size() >= 10;
}

CameraMotion Fit(const std::vector<cv::Point2f>& good_prev, const std::vector<cv::Point2f>& good_curr,
                 double ransac_px, cv::Size frame) {
    CameraMotion out;
    out.frame = frame;
    cv::Mat inliers;
    const cv::Mat t = cv::estimateAffinePartial2D(good_prev, good_curr, inliers, cv::RANSAC, ransac_px);
    if (t.empty()) return out;
    out.ok = true;
    out.m = cv::Matx23d(t);
    if (!inliers.empty())
        out.inliers = static_cast<double>(cv::countNonZero(inliers)) / static_cast<double>(inliers.total());
    return out;
}

}  // namespace

double CameraMotion::Scale() const { return std::hypot(m(0, 0), m(1, 0)); }

double CameraMotion::Angle() const { return std::atan2(m(1, 0), m(0, 0)); }

cv::Point2d CameraMotion::Shift(const cv::Point2d& p) const {
    // m * p - p, written so that an identity matrix gives exactly 0.
    return {(m(0, 0) - 1.0) * p.x + m(0, 1) * p.y + m(0, 2), m(1, 0) * p.x + (m(1, 1) - 1.0) * p.y + m(1, 2)};
}

bool CameraMotion::Plausible() const {
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c)
            if (!std::isfinite(m(r, c))) return false;
    if (std::abs(Scale() - 1.0) > kMaxZoomStep || std::abs(Angle()) > kMaxTurnStep) return false;
    if (frame.width > 0 && frame.height > 0) {
        const cv::Point2d centre = Shift({frame.width / 2.0, frame.height / 2.0});
        if (std::abs(centre.x) > kMaxShiftShare * frame.width || std::abs(centre.y) > kMaxShiftShare * frame.height)
            return false;
    }
    return true;
}

CameraMotion EstimateCameraMotion(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                                  double ransac_px) {
    std::vector<cv::Point2f> good_prev, good_curr;
    if (!MatchedFeatures(prev_gray, gray, excludes, &good_prev, &good_curr)) {
        CameraMotion none;
        none.frame = prev_gray.size();
        return none;
    }
    return Fit(good_prev, good_curr, ransac_px, prev_gray.size());
}

CameraFits EstimateCameraFits(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                              bool want_coarse) {
    CameraFits out;
    out.fine.frame = out.coarse.frame = prev_gray.size();
    std::vector<cv::Point2f> good_prev, good_curr;
    if (!MatchedFeatures(prev_gray, gray, excludes, &good_prev, &good_curr)) return out;
    out.fine = Fit(good_prev, good_curr, CameraMotion::kRansacPx, prev_gray.size());
    if (want_coarse) out.coarse = Fit(good_prev, good_curr, CameraMotion::kRansacPx07, prev_gray.size());
    return out;
}

double CameraMotionY(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                     double* inlier_fraction) {
    const CameraMotion c = EstimateCameraMotion(prev_gray, gray, excludes, CameraMotion::kRansacPx07);
    if (inlier_fraction) *inlier_fraction = c.inliers;
    return c.ok ? c.m(1, 2) : 0.0;
}

void HoldLostPositions(std::vector<double>& v, const std::vector<uint8_t>& lost, const std::vector<int>& cuts) {
    for (size_t i = 1; i < v.size() && i < lost.size(); ++i)
        if (lost[i] == kLostTracker && std::find(cuts.begin(), cuts.end(), static_cast<int>(i)) == cuts.end())
            v[i] = v[i - 1];
}

}  // namespace samn::engine
