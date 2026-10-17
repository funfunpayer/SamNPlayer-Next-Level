#include "samn/vision/tracker.h"

#include <cmath>
#include <cstdlib>
#include <random>

#include <opencv2/imgproc.hpp>
#include <opencv2/tracking.hpp>

namespace samn::vision {

namespace {

class CsrtTracker : public Tracker {
public:
    explicit CsrtTracker(cv::Ptr<cv::TrackerCSRT> t) : t_(std::move(t)) {}
    void Init(const cv::Mat& frame, const cv::Rect& box) override { t_->init(frame, box); }
    std::optional<cv::Rect> Update(const cv::Mat& frame) override {
        cv::Rect box;
        if (!t_->update(frame, box)) return std::nullopt;
        return box;
    }
    const char* name() const override { return "CSRT"; }

private:
    cv::Ptr<cv::TrackerCSRT> t_;
};

// Textured background with a textured patch at (x, y): a moving pattern a
// correlation tracker can follow (synthetic, generated, GD-22).
cv::Mat PatternFrame(const cv::Mat& background, const cv::Mat& patch, int x, int y) {
    cv::Mat f = background.clone();
    patch.copyTo(f(cv::Rect(x, y, patch.cols, patch.rows)));
    return f;
}

cv::Mat Noise(int w, int h, unsigned seed) {
    cv::Mat m(h, w, CV_8UC3);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0, 255);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) m.at<cv::Vec3b>(r, c) = cv::Vec3b(uchar(d(rng)), uchar(d(rng)), uchar(d(rng)));
    cv::GaussianBlur(m, m, cv::Size(5, 5), 0);
    return m;
}

}  // namespace

std::unique_ptr<Tracker> CreateTracker(TrackerKind kind, std::string* error) {
    switch (kind) {
    case TrackerKind::kCsrt: {
        cv::Ptr<cv::TrackerCSRT> t;
        try {
            t = cv::TrackerCSRT::create();
        } catch (const cv::Exception& e) {
            if (error) *error = std::string("CSRT tracker cannot be created: ") + e.what();
            return nullptr;
        }
        if (!t) {
            if (error) *error = "CSRT tracker is not in this OpenCV build (contrib tracking missing)";
            return nullptr;
        }
        return std::make_unique<CsrtTracker>(std::move(t));
    }
    }
    if (error) *error = "unknown tracker kind";
    return nullptr;
}

std::string VisionSelfCheck() {
    std::string error;
    auto tracker = CreateTracker(TrackerKind::kCsrt, &error);
    if (!tracker) return error;
    const cv::Mat background = Noise(320, 240, 1);
    const cv::Mat patch = Noise(32, 32, 2);
    int x = 60, y = 100;
    try {
        tracker->Init(PatternFrame(background, patch, x, y), cv::Rect(x, y, 32, 32));
        for (int i = 0; i < 40; ++i) {
            x += 3;
            y += (i % 2) ? 1 : -1;
            const auto box = tracker->Update(PatternFrame(background, patch, x, y));
            if (!box) return "CSRT lost a plain moving pattern at frame " + std::to_string(i + 1);
            if (std::abs(box->x - x) > 3 || std::abs(box->y - y) > 3)
                return "CSRT is off by more than 3 px on a plain moving pattern (frame " + std::to_string(i + 1) + ")";
        }
    } catch (const cv::Exception& e) {
        return std::string("CSRT failed: ") + e.what();
    }
    return {};
}

std::string OpenCvVersion() { return std::string("OpenCV ") + SAMN_OPENCV_VERSION; }

}  // namespace samn::vision
