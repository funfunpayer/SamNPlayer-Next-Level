#include "samn/vision/picture_signals.h"

#include <opencv2/imgproc.hpp>

#include "samn/vision/frame_reader.h"

namespace samn::vision {

PictureSignals MeasurePictureSignals(const std::filesystem::path& video, const PictureSignalOptions& o,
                                     std::string* error) {
    PictureSignals out;
    out.step_ms = std::max<int64_t>(1, o.step_ms);
    auto reader = FrameReader::Open(video, error);
    if (!reader) return out;
    const int64_t total = reader->frameCount();
    VideoFrame frame;
    cv::Mat small, grey, previous, diff;
    int64_t next_ms = 0;
    int last_progress = -1;
    while (reader->Read(frame)) {
        if (o.cancel && o.cancel()) {
            if (error) *error = "canceled";
            return {};
        }
        const int64_t ms = frame.pts_us / 1000;
        if (ms < next_ms) continue;
        // One sample per step; a gap in the stream repeats nothing.
        next_ms = (ms / out.step_ms + 1) * out.step_ms;
        cv::resize(frame.bgr, small, cv::Size(64, 36), 0, 0, cv::INTER_AREA);
        cv::cvtColor(small, grey, cv::COLOR_BGR2GRAY);
        out.luma.push_back(cv::mean(grey)[0] / 255.0);
        if (previous.empty()) {
            out.motion.push_back(0);
        } else {
            cv::absdiff(grey, previous, diff);
            out.motion.push_back(cv::mean(diff)[0] / 255.0);
        }
        grey.copyTo(previous);
        if (o.progress && total > 0) {
            const int p = int(std::min<int64_t>(99, frame.index * 100 / total));
            if (p != last_progress) o.progress(last_progress = p);
        }
    }
    if (out.luma.empty() && error) *error = "no frames";
    if (o.progress) o.progress(100);
    return out;
}

}  // namespace samn::vision
