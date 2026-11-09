#include "samn/engine/label_tracking.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <opencv2/imgproc.hpp>

#include "samn/vision/canceled.h"
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
    // The sample loop below advances by the step: zero or less never ends.
    if (o.step_ms <= 0) {
        if (error) *error = "the sample step must be positive";
        return std::nullopt;
    }
    auto reader = vision::FrameReader::Open(video, error);
    if (!reader || !reader->SeekMs(from_ms)) {
        if (error && error->empty()) *error = "cannot seek in the video";
        return std::nullopt;
    }
    const int window = std::max(1, o.window_frames);
    BetweenKeysStats local;
    BetweenKeysStats& st = o.stats ? *o.stats : local;
    st = {};

    // One window of small frames, reused for every chunk of both runs, and
    // one read buffer: nothing else holds pixels. Times and the sample
    // indices are all that is kept for the whole stretch.
    std::vector<cv::Mat> win(static_cast<size_t>(window));
    vision::VideoFrame f;
    double scale = 1.0;
    const auto shrink = [&scale](const vision::VideoFrame& src, cv::Mat& dst) {
        if (scale < 1.0) cv::resize(src.bgr, dst, cv::Size(), scale, scale, cv::INTER_AREA);
        else src.bgr.copyTo(dst);
    };

    std::vector<int64_t> times;
    std::vector<size_t> samples;  // the first frame at or after each step
    std::vector<std::optional<LabelBox>> fwd_at;
    int64_t next_step = from_ms + o.step_ms;
    const int64_t span = to_ms - from_ms;

    BetweenKeysResult out;
    const auto cancelled = [&] {
        if (o.cancel && o.cancel()) {
            if (error) *error = kCanceled;
            return true;
        }
        return false;
    };

    // Forward run, streaming: each frame is read, tracked and dropped.
    {
        auto tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, error);
        if (!tracker) return std::nullopt;
        bool lost = false;
        while (reader->Read(f)) {
            ++st.frames_decoded;
            if (cancelled()) return std::nullopt;
            const int64_t ms = f.pts_us / 1000;
            if (ms > to_ms) break;
            const size_t i = times.size();
            if (i == 0) scale = std::min(1.0, double(o.max_side) / std::max(f.bgr.cols, f.bgr.rows));
            if (int(i) >= o.max_frames) {
                if (error) *error = "the stretch is too long; add a key in between";
                return std::nullopt;
            }
            times.push_back(ms);
            fwd_at.emplace_back();
            if (next_step < to_ms && ms >= next_step) {
                samples.push_back(i);
                while (next_step <= ms) next_step += o.step_ms;
            }
            if (i == 0) {
                shrink(f, win[0]);
                const cv::Rect init = ToRect(from_box, scale, win[0].size());
                if (init.width < 2 || init.height < 2) {
                    if (error) *error = "the key's box is outside the frame";
                    return std::nullopt;
                }
                tracker->Init(win[0], init);
                fwd_at[0] = from_box;
            } else if (!lost) {
                shrink(f, win[0]);
                if (auto r = tracker->Update(win[0])) fwd_at[i] = FromRect(*r, scale);
                else lost = true;  // gone stays gone for this run
            }
            if (o.progress) o.progress(int(std::clamp<int64_t>((ms - from_ms) * 50 / span, 0, 50)));
        }
    }
    const size_t n = times.size();
    if (n < 2) {
        if (error) *error = "no frames between the keys";
        return std::nullopt;
    }
    for (const size_t i : samples) out.forward.push_back({times[i], fwd_at[i]});
    fwd_at = {};

    // Backward run: the frames come back in chunks, each decoded forward from
    // a frame-exact seek and tracked from its last frame to its first. The
    // next chunk is decoded on another thread while this one is tracked (two
    // buffers), so the second read of the stretch costs wall time only where
    // there is no spare core.
    {
        auto tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, error);
        if (!tracker) return std::nullopt;
        struct Chunk { size_t lo, hi; };
        std::vector<Chunk> chunks;
        for (size_t hi = n; hi > 0;) {
            const size_t lo = hi > size_t(window) ? hi - size_t(window) : 0;
            chunks.push_back({lo, hi});
            hi = lo;
        }
        std::vector<cv::Mat> spare(static_cast<size_t>(window));
        std::vector<cv::Mat>* bufs[2] = {&win, &spare};
        struct Decoded { std::string error; int frames = 0; };
        const auto decode = [&](const Chunk& c, std::vector<cv::Mat>& dst) {
            Decoded d;
            if (!reader->SeekMs(times[c.lo])) {
                d.error = "cannot seek in the video";
                return d;
            }
            vision::VideoFrame g;
            for (size_t i = c.lo; i < c.hi; ++i) {
                if (!reader->Read(g) || g.pts_us / 1000 != times[i]) {
                    d.error = "the video changed between two reads";
                    return d;
                }
                ++d.frames;
                shrink(g, dst[i - c.lo]);
            }
            return d;
        };
        std::vector<std::optional<LabelBox>> at(n);
        bool lost = false;
        auto pending = std::async(std::launch::async, decode, chunks[0], std::ref(*bufs[0]));
        for (size_t c = 0; c < chunks.size() && !lost; ++c) {
            const Decoded d = pending.get();
            st.frames_decoded += d.frames;
            if (!d.error.empty()) {
                if (error) *error = d.error;
                return std::nullopt;
            }
            if (c + 1 < chunks.size()) pending = std::async(std::launch::async, decode, chunks[c + 1], std::ref(*bufs[(c + 1) & 1]));
            const std::vector<cv::Mat>& cur = *bufs[c & 1];
            for (size_t i = chunks[c].hi; i-- > chunks[c].lo;) {
                const cv::Mat& frame = cur[i - chunks[c].lo];
                if (i == n - 1) {
                    const cv::Rect init = ToRect(to_box, scale, frame.size());
                    if (init.width < 2 || init.height < 2) {
                        if (error) *error = "the key's box is outside the frame";
                        return std::nullopt;
                    }
                    tracker->Init(frame, init);
                    at[i] = to_box;
                    continue;
                }
                if (cancelled()) return std::nullopt;
                if (auto r = tracker->Update(frame)) at[i] = FromRect(*r, scale);
                else {
                    lost = true;  // gone stays gone: the frames before are not tracked
                    break;
                }
                if (o.progress) o.progress(50 + int((n - 1 - i) * 50 / n));
            }
        }
        st.peak_frames_held = int(std::count_if(win.begin(), win.end(), [](const cv::Mat& m) { return !m.empty(); }) +
                                  std::count_if(spare.begin(), spare.end(), [](const cv::Mat& m) { return !m.empty(); }));
        for (const size_t i : samples) out.backward.push_back({times[i], at[i]});
    }
    if (o.progress) o.progress(100);
    return out;
}

}  // namespace samn::engine
