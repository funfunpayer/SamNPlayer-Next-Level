#include "track_parts.h"

#include <algorithm>

#include <opencv2/imgproc.hpp>

namespace samn::engine {

double HistCorrelation(const cv::Mat& a, const cv::Mat& b) {
    const int hist_size = 64;
    const int channels[] = {0};
    const float range[] = {0, 256};
    const float* ranges[] = {range};
    cv::Mat h1, h2;
    cv::calcHist(&a, 1, channels, cv::Mat(), h1, 1, &hist_size, ranges);
    cv::calcHist(&b, 1, channels, cv::Mat(), h2, 1, &hist_size, ranges);
    cv::normalize(h1, h1);
    cv::normalize(h2, h2);
    return cv::compareHist(h1, h2, cv::HISTCMP_CORREL);
}

double SignatureDiff(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat sa, sb, diff;
    cv::resize(a, sa, cv::Size(64, 48), 0, 0, cv::INTER_AREA);
    cv::resize(b, sb, cv::Size(64, 48), 0, 0, cv::INTER_AREA);
    sa.convertTo(sa, CV_32F);
    sb.convertTo(sb, CV_32F);
    cv::absdiff(sa, sb, diff);
    return cv::mean(diff)[0];
}

bool IsSceneCut(const cv::Mat& prev_gray, const cv::Mat& gray) {
    return HistCorrelation(prev_gray, gray) < 0.5 || SignatureDiff(prev_gray, gray) > 12.0;
}

bool AdaptiveSceneCut::Test(const cv::Mat& prev_gray, const cv::Mat& gray) {
    const double sig = SignatureDiff(prev_gray, gray);
    double median = 0;
    if (past_.size() >= 3) {
        std::vector<double> v(past_);
        const auto mid = v.begin() + static_cast<long>(v.size() / 2);
        std::nth_element(v.begin(), mid, v.end());
        median = *mid;
    }
    const bool cut = HistCorrelation(prev_gray, gray) < 0.5 || sig > std::max(12.0, factor_ * median);
    // Every difference joins the history, a cut's too: the median shrugs off
    // one spike, and clearing would leave a fast shot without a baseline.
    past_.push_back(sig);
    if (static_cast<int>(past_.size()) > window_) past_.erase(past_.begin());
    return cut;
}

}  // namespace samn::engine
