#pragma once

// Video frames for analysis (P3.0). Every frame carries its presentation
// time in microseconds of video time, and every public time is video time
// (LESSONS GD-07: 0.7's clock-after-seek bugs came from frame indices and
// a clock that restarted at 0 after a seek).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

namespace samn::vision {

struct VideoFrame {
    cv::Mat bgr;          // 8-bit, 3 channels
    int64_t index = 0;    // frame number from the start of the stream
    int64_t pts_us = 0;   // presentation time, video time
};

class FrameReader {
public:
    // nullptr with *error set when the file cannot be opened or decoded.
    static std::unique_ptr<FrameReader> Open(const std::filesystem::path& path, std::string* error);
    ~FrameReader();
    FrameReader(const FrameReader&) = delete;
    FrameReader& operator=(const FrameReader&) = delete;

    double fps() const { return fps_; }
    int64_t frameCount() const { return frame_count_; }
    cv::Size size() const { return size_; }

    // The next frame; false at the end or on a decode error.
    bool Read(VideoFrame& out);
    // Frame-exact: the next Read returns the first frame whose time is at
    // or after ms (clamped to the stream). False when the stream cannot seek.
    bool SeekMs(int64_t ms);

private:
    FrameReader() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    double fps_ = 0;
    int64_t frame_count_ = 0;
    cv::Size size_;
};

}  // namespace samn::vision
