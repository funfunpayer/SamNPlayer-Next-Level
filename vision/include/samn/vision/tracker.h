#pragma once

// One place that creates trackers (LESSONS GD-02): 0.7 fell back to a
// weaker tracker on systems without OpenCV contrib and nobody noticed.
// Here a missing tracker is an error, and the self-check proves at start-up
// and in CI that CSRT runs on this build.

#include <memory>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace samn::vision {

enum class TrackerKind { kCsrt };

class Tracker {
public:
    virtual ~Tracker() = default;
    virtual void Init(const cv::Mat& frame, const cv::Rect& box) = 0;
    // The box in this frame, or nullopt when the tracker lost it.
    virtual std::optional<cv::Rect> Update(const cv::Mat& frame) = 0;
    virtual const char* name() const = 0;
};

// nullptr with *error set when the kind is not available; never another kind.
std::unique_ptr<Tracker> CreateTracker(TrackerKind kind, std::string* error);

// Runs CSRT on a generated moving pattern. "" when it tracks within 3 px,
// else the reason. Cheap enough for app start-up.
std::string VisionSelfCheck();

// "OpenCV 4.6.0" for the log (GD-02: the log names tracker and version).
std::string OpenCvVersion();

}  // namespace samn::vision
