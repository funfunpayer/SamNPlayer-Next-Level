#include "samn/engine/label_tracking.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

#include "samn/vision/frame_reader.h"
#include "samn/vision/tracker.h"

namespace samn::engine {

namespace {

cv::Rect ToRect(const LabelBox& b, double s, const cv::Size& size) {
    cv::Rect r(int(std::lround(b.x * s)), int(std::lround(b.y * s)), int(std::lround(b.w * s)),
               int(std::lround(b.h * s)));
    r &= cv::Rect(0, 0, size.width, size.height);
    return r;
}

LabelBox FromRect(const cv::Rect& r, double s) { return {r.x / s, r.y / s, r.width / s, r.height / s}; }

}  // namespace

std::optional<BetweenKeysResult> TrackBetweenKeys(const std::filesystem::path& video, int64_t from_ms,
                                                  const LabelBox& from_box, int64_t to_ms, const LabelBox& to_box,
                                                  const BetweenKeysOptions& o, std::string* error) {
    if (to_ms <= from_ms) {
        if (error) *error = "the second key is not after the first";
        return std::nullopt;
    }
    auto reader = vision::FrameReader::Open(video, error);
    if (!reader || !reader->SeekMs(from_ms)) {
        if (error && error->empty()) *error = "cannot seek in the video";
        return std::nullopt;
    }
    // The frames of the stretch, small; the sample times are the frames
    // that cross a step.
    std::vector<cv::Mat> frames;
    std::vector<int64_t> times;
    double scale = 1.0;
    vision::VideoFrame f;
    const int64_t span = to_ms - from_ms;
    while (reader->Read(f)) {
        if (o.cancel && o.cancel()) {
            if (error) *error = "canceled";
            return std::nullopt;
        }
        const int64_t ms = f.pts_us / 1000;
        if (ms > to_ms) break;
        if (frames.empty()) scale = std::min(1.0, double(o.max_side) / std::max(f.bgr.cols, f.bgr.rows));
        cv::Mat small;
        if (scale < 1.0) cv::resize(f.bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
        else small = f.bgr.clone();
        if (int(frames.size()) >= o.max_frames) {
            if (error) *error = "the stretch is too long; add a key in between";
            return std::nullopt;
        }
        frames.push_back(std::move(small));
        times.push_back(ms);
        if (o.progress) o.progress(int(std::clamp<int64_t>((ms - from_ms) * 50 / span, 0, 50)));
    }
    if (frames.size() < 2) {
        if (error) *error = "no frames between the keys";
        return std::nullopt;
    }
    // The sample frames: the first frame at or after each step.
    std::vector<size_t> samples;
    for (int64_t next = from_ms + o.step_ms; next < to_ms; next += o.step_ms) {
        const auto it = std::lower_bound(times.begin(), times.end(), next);
        if (it == times.end()) break;
        const size_t i = size_t(it - times.begin());
        if (samples.empty() || samples.back() != i) samples.push_back(i);
    }

    BetweenKeysResult out;
    const auto run = [&](bool forward) -> bool {
        auto tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, error);
        if (!tracker) return false;
        const size_t n = frames.size();
        const size_t start = forward ? 0 : n - 1;
        const cv::Rect init = ToRect(forward ? from_box : to_box, scale, frames[start].size());
        if (init.width < 2 || init.height < 2) {
            if (error) *error = "the key's box is outside the frame";
            return false;
        }
        tracker->Init(frames[start], init);
        std::vector<std::optional<LabelBox>> at(n);
        at[start] = forward ? from_box : to_box;
        bool lost = false;
        for (size_t k = 1; k < n; ++k) {
            if (o.cancel && o.cancel()) {
                if (error) *error = "canceled";
                return false;
            }
            const size_t i = forward ? k : n - 1 - k;
            if (!lost) {
                if (auto r = tracker->Update(frames[i])) at[i] = FromRect(*r, scale);
                else lost = true;  // gone stays gone for this run
            }
            if (o.progress) o.progress(50 + int(k * 25 / n) + (forward ? 0 : 25));
        }
        auto& dst = forward ? out.forward : out.backward;
        for (const size_t i : samples) dst.push_back({times[i], at[i]});
        return true;
    };
    if (!run(true) || !run(false)) return std::nullopt;
    if (o.progress) o.progress(100);
    return out;
}

}  // namespace samn::engine
