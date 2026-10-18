#include "track_parts.h"

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

}  // namespace samn::engine
