#include <algorithm>
#include <cmath>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include "track_parts.h"

namespace samn::engine {

double CameraMotion::Scale() const { return std::hypot(m(0, 0), m(1, 0)); }

double CameraMotion::Angle() const { return std::atan2(m(1, 0), m(0, 0)); }

cv::Point2d CameraMotion::Shift(const cv::Point2d& p) const {
    // m * p - p, written so that an identity matrix gives exactly 0.
    return {(m(0, 0) - 1.0) * p.x + m(0, 1) * p.y + m(0, 2), m(1, 0) * p.x + (m(1, 1) - 1.0) * p.y + m(1, 2)};
}

CameraMotion EstimateCameraMotion(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                                  double ransac_px) {
    CameraMotion out;
    const int h = prev_gray.rows, w = prev_gray.cols, pad = 10;
    cv::Mat mask(h, w, CV_8U, cv::Scalar(255));
    for (const cv::Rect& r : excludes) {
        const int x0 = std::max(0, r.x - pad), y0 = std::max(0, r.y - pad);
        const int x1 = std::min(w, r.x + r.width + pad), y1 = std::min(h, r.y + r.height + pad);
        if (x1 > x0 && y1 > y0) mask(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(0);
    }
    std::vector<cv::Point2f> prev_pts;
    cv::goodFeaturesToTrack(prev_gray, prev_pts, 200, 0.01, 20, mask, 7);
    if (prev_pts.size() < 10) return out;

    std::vector<cv::Point2f> curr_pts;
    std::vector<uchar> status;
    std::vector<float> err;
    cv::calcOpticalFlowPyrLK(prev_gray, gray, prev_pts, curr_pts, status, err);
    std::vector<cv::Point2f> good_prev, good_curr;
    for (size_t i = 0; i < status.size(); ++i) {
        if (!status[i]) continue;
        good_prev.push_back(prev_pts[i]);
        good_curr.push_back(curr_pts[i]);
    }
    if (good_prev.size() < 10) return out;

    cv::Mat inliers;
    const cv::Mat t = cv::estimateAffinePartial2D(good_prev, good_curr, inliers, cv::RANSAC, ransac_px);
    if (t.empty()) return out;
    out.ok = true;
    out.m = cv::Matx23d(t);
    if (!inliers.empty())
        out.inliers = static_cast<double>(cv::countNonZero(inliers)) / static_cast<double>(inliers.total());
    return out;
}

double CameraMotionY(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes,
                     double* inlier_fraction) {
    const CameraMotion c = EstimateCameraMotion(prev_gray, gray, excludes, CameraMotion::kRansacPx07);
    if (inlier_fraction) *inlier_fraction = c.inliers;
    return c.ok ? c.m(1, 2) : 0.0;
}

}  // namespace samn::engine
