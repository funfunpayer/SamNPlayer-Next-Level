#include <algorithm>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include "track_parts.h"

namespace samn::engine {

double CameraMotionY(const cv::Mat& prev_gray, const cv::Mat& gray, const std::vector<cv::Rect>& excludes) {
    const int h = prev_gray.rows, w = prev_gray.cols, pad = 10;
    cv::Mat mask(h, w, CV_8U, cv::Scalar(255));
    for (const cv::Rect& r : excludes) {
        const int x0 = std::max(0, r.x - pad), y0 = std::max(0, r.y - pad);
        const int x1 = std::min(w, r.x + r.width + pad), y1 = std::min(h, r.y + r.height + pad);
        if (x1 > x0 && y1 > y0) mask(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(0);
    }
    std::vector<cv::Point2f> prev_pts;
    cv::goodFeaturesToTrack(prev_gray, prev_pts, 200, 0.01, 20, mask, 7);
    if (prev_pts.size() < 10) return 0.0;

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
    if (good_prev.size() < 10) return 0.0;

    cv::Mat inliers;
    const cv::Mat t = cv::estimateAffinePartial2D(good_prev, good_curr, inliers, cv::RANSAC);
    if (t.empty()) return 0.0;
    return t.at<double>(1, 2);
}

}  // namespace samn::engine
