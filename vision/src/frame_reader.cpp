#include "samn/vision/frame_reader.h"

#include <algorithm>
#include <cmath>

#include <opencv2/videoio.hpp>

namespace samn::vision {

struct FrameReader::Impl {
    cv::VideoCapture cap;
    int64_t next_index = 0;
    int64_t grabbed_index = 0;  // the frame the last successful Grab advanced to
    int64_t grabbed_pts_us = 0;
    // Frames grabbed in a row from the stream's first frame that all reported
    // time 0; -1 once a later time was seen or the reader sought elsewhere.
    int zero_run = 0;
    // The time of the frame grabbed before this one in the same run of grabs;
    // -1 after a seek or at the start.
    int64_t last_pts_us = -1;
};

namespace {
// At most this many frames at time 0 from the stream's start count as real
// (pairs and triples of equal timestamps exist; more is a decoder that
// reports no times).
constexpr int kMaxZeroRun = 3;
}  // namespace

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
    const double tag = r->impl_->cap.get(cv::CAP_PROP_FOURCC);
    r->fourcc_ = tag > 0 && tag < 4294967296.0 ? static_cast<uint32_t>(tag) : 0;
    r->frame_count_ = static_cast<int64_t>(std::llround(r->impl_->cap.get(cv::CAP_PROP_FRAME_COUNT)));
    r->size_ = cv::Size(static_cast<int>(r->impl_->cap.get(cv::CAP_PROP_FRAME_WIDTH)),
                        static_cast<int>(r->impl_->cap.get(cv::CAP_PROP_FRAME_HEIGHT)));
    if (!(r->fps_ > 0) || r->size_.area() <= 0) return fail("video has no usable frame rate or size");
    return r;
}

bool FrameReader::Grab(int64_t* pts_us) {
    if (!impl_->cap.grab()) return false;
    const int64_t index = impl_->next_index++;
    // The decoder's time of the frame just grabbed. A time of 0 after the first
    // frame is real only while every frame since the stream's start had it, and
    // for at most kMaxZeroRun frames (some streams put two or three frames at
    // t=0). Anywhere else 0 means the decoder did not say: some OpenCV builds
    // report 0 for the frames a decoder hands out at the end of the stream, and
    // a negative value is never a time. Those frames get the nominal time of
    // the frame number (constant frame rate), or one frame step after the frame
    // before when that nominal time would not be later (variable frame rate).
    // A stream whose decoder reports no times at all therefore has its first
    // kMaxZeroRun frames at 0 and nominal times from there on.
    const double ms = impl_->cap.get(cv::CAP_PROP_POS_MSEC);
    impl_->grabbed_index = index;
    bool real = ms > 0;
    if (ms == 0 && impl_->zero_run >= 0 && impl_->zero_run < kMaxZeroRun && index == impl_->zero_run) {
        real = true;
        ++impl_->zero_run;
    } else {
        impl_->zero_run = -1;
    }
    int64_t pts = static_cast<int64_t>(std::llround(real ? ms * 1000.0 : index * 1e6 / fps_));
    if (!real && impl_->last_pts_us >= 0 && pts <= impl_->last_pts_us)
        pts = impl_->last_pts_us + static_cast<int64_t>(std::llround(1e6 / fps_));
    impl_->grabbed_pts_us = pts;
    impl_->last_pts_us = pts;
    if (pts_us) *pts_us = impl_->grabbed_pts_us;
    return true;
}

bool FrameReader::Retrieve(VideoFrame& out) {
    if (!impl_->cap.retrieve(out.bgr) || out.bgr.empty()) return false;
    out.index = impl_->grabbed_index;
    out.pts_us = impl_->grabbed_pts_us;
    return true;
}

bool FrameReader::Read(VideoFrame& out) {
    // cap.read() is grab plus retrieve; a failed retrieve ends the stream as before.
    return Grab(nullptr) && Retrieve(out);
}

bool FrameReader::SeekMs(int64_t ms) {
    // Callers hold whole milliseconds, so a frame's time arrives rounded up by
    // up to half a ms; tolerate that, or the frame after it would be returned.
    const double t = std::max(0.0, static_cast<double>(ms) - 0.5);
    int64_t target = static_cast<int64_t>(std::ceil(t * fps_ / 1000.0 - 1e-6));
    if (frame_count_ > 0) target = std::min(target, frame_count_ - 1);
    // A target inside a possible run of frames at t=0 is reached from frame 0,
    // so those frames get the same times as when the stream is read from its
    // start.
    const bool from_start = target > 0 && target < kMaxZeroRun;
    if (!impl_->cap.set(cv::CAP_PROP_POS_FRAMES, from_start ? 0.0 : static_cast<double>(target))) return false;
    impl_->next_index = from_start ? 0 : target;
    impl_->zero_run = impl_->next_index == 0 ? 0 : -1;
    impl_->last_pts_us = -1;
    for (int64_t i = 0; from_start && i < target; ++i)
        if (!Grab(nullptr)) return false;
    return true;
}

}  // namespace samn::vision
