#include "samn/vision/frame_reader.h"

#include <algorithm>
#include <cmath>

#include <opencv2/videoio.hpp>

namespace samn::vision {

struct FrameReader::Impl {
    cv::VideoCapture cap;
    int64_t next_index = 0;
};

FrameReader::~FrameReader() = default;

std::unique_ptr<FrameReader> FrameReader::Open(const std::filesystem::path& path, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why + ": " + path.u8string();
        return std::unique_ptr<FrameReader>();
    };
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return fail("video not found");
    std::unique_ptr<FrameReader> r(new FrameReader);
    r->impl_ = std::make_unique<Impl>();
    // Always the FFmpeg backend (D-133): the same decoder and timestamps on
    // every system, never a silent switch to another backend.
    if (!r->impl_->cap.open(path.u8string(), cv::CAP_FFMPEG) || !r->impl_->cap.isOpened())
        return fail("video cannot be opened");
    r->fps_ = r->impl_->cap.get(cv::CAP_PROP_FPS);
    r->frame_count_ = static_cast<int64_t>(std::llround(r->impl_->cap.get(cv::CAP_PROP_FRAME_COUNT)));
    r->size_ = cv::Size(static_cast<int>(r->impl_->cap.get(cv::CAP_PROP_FRAME_WIDTH)),
                        static_cast<int>(r->impl_->cap.get(cv::CAP_PROP_FRAME_HEIGHT)));
    if (!(r->fps_ > 0) || r->size_.area() <= 0) return fail("video has no usable frame rate or size");
    return r;
}

bool FrameReader::Read(VideoFrame& out) {
    if (!impl_->cap.read(out.bgr) || out.bgr.empty()) return false;
    out.index = impl_->next_index++;
    // The decoder's time of the frame just read; streams without one get the
    // nominal time of the frame number (constant frame rate).
    const double ms = impl_->cap.get(cv::CAP_PROP_POS_MSEC);
    out.pts_us = (ms > 0 || out.index == 0) ? static_cast<int64_t>(std::llround(ms * 1000.0))
                                            : static_cast<int64_t>(std::llround(out.index * 1e6 / fps_));
    return true;
}

bool FrameReader::SeekMs(int64_t ms) {
    int64_t target = static_cast<int64_t>(std::ceil(std::max<int64_t>(0, ms) * fps_ / 1000.0 - 1e-6));
    if (frame_count_ > 0) target = std::min(target, frame_count_ - 1);
    if (!impl_->cap.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(target))) return false;
    impl_->next_index = target;
    return true;
}

}  // namespace samn::vision
