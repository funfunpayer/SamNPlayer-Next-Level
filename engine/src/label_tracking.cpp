#include "samn/engine/label_tracking.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
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
    using Clock = std::chrono::steady_clock;
    const auto ms_since = [](Clock::time_point t) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    };
    const int base_window = std::max(1, o.window_frames);
    const int max_window = std::max(base_window, o.max_window_frames);
    BetweenKeysStats local;
    BetweenKeysStats& st = o.stats ? *o.stats : local;
    st.peak_frames_held = 0;
    st.frames_decoded = 0;
    st.window_used = 0;

    // Frames alive at once: the small buffers (reused; counted when first
    // filled) and the two full-size read buffers. Times and the sample
    // indices are all that is kept for the whole stretch.
    std::atomic<int> held{0};
    const auto note_held = [&] {
        const int h = ++held;
        int p = st.peak_frames_held.load();
        while (h > p && !st.peak_frames_held.compare_exchange_weak(p, h)) {}
    };
    double scale = 1.0;
    const auto shrink = [&scale, &note_held](const vision::VideoFrame& src, cv::Mat& dst) {
        const bool fresh = dst.empty();
        if (scale < 1.0) cv::resize(src.bgr, dst, cv::Size(), scale, scale, cv::INTER_AREA);
        else src.bgr.copyTo(dst);
        if (fresh) note_held();
    };
    std::vector<cv::Mat> win(1);  // the forward run's one small frame
    vision::VideoFrame f;
    bool have_f = false;

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

    // Forward run, streaming: each frame is read, tracked and dropped. (A
    // missing tracker is reported before the stretch is read, not after.)
    cv::Size small_size;
    double update_ms = 0;  // time in the tracker during the forward run: what a frame costs to track
    int updates = 0;
    {
        auto tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, error);
        if (!tracker) return std::nullopt;
        bool lost = false;
        bool bad_box = false;  // reported after the stretch checks, as before
        for (;;) {
            int64_t pts_us = 0;
            if (!reader->Grab(&pts_us)) break;
            const bool track_it = !lost && !bad_box;
            // A frame that nothing will track is grabbed only, not converted.
            if (track_it || times.empty()) {
                if (!reader->Retrieve(f)) break;
                if (!have_f) {
                    have_f = true;
                    note_held();
                }
            }
            ++st.frames_decoded;
            if (cancelled()) return std::nullopt;
            const int64_t ms = pts_us / 1000;
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
                small_size = win[0].size();
                const cv::Rect init = ToRect(from_box, scale, small_size);
                if (init.width < 2 || init.height < 2) {
                    bad_box = true;
                } else {
                    tracker->Init(win[0], init);
                    fwd_at[0] = from_box;
                }
            } else if (track_it) {
                shrink(f, win[0]);
                const auto t0 = Clock::now();
                const auto r = tracker->Update(win[0]);
                update_ms += ms_since(t0);
                ++updates;
                if (r) fwd_at[i] = FromRect(*r, scale);
                else lost = true;  // gone stays gone for this run
            }
            if (o.progress) o.progress(int(std::clamp<int64_t>((ms - from_ms) * 50 / span, 0, 50)));
        }
        if (times.size() >= 2 && bad_box) {
            if (error) *error = "the key's box is outside the frame";
            return std::nullopt;
        }
    }
    const size_t n = times.size();
    if (n < 2) {
        if (error) *error = "no frames between the keys";
        return std::nullopt;
    }
    if (ToRect(to_box, scale, small_size).width < 2 || ToRect(to_box, scale, small_size).height < 2) {
        if (error) *error = "the key's box is outside the frame";
        return std::nullopt;
    }
    for (const size_t i : samples) out.forward.push_back({times[i], fwd_at[i]});
    fwd_at = {};

    // Backward run: the frames come back in chunks, each decoded forward from
    // a seek and tracked from its last frame to its first. The next chunk is
    // decoded on another thread while this one is tracked (two buffers).
    //
    // A seek goes to the key frame before the target, so on a stream with a
    // long GOP every chunk pays for the frames before it; the window grows
    // (up to max_window_frames) when the measured cost of a seek says so.
    // The frame wanted is found by its time: a seek lands by the nominal frame
    // rate, which on a variable-rate stream can be after the frame, so the
    // seek is retried further back, down to the first key's own position.
    {
        auto tracker = vision::CreateTracker(vision::TrackerKind::kCsrt, error);
        if (!tracker) return std::nullopt;
        struct Chunk { size_t lo, hi; };
        std::vector<cv::Mat> bufs[2] = {std::vector<cv::Mat>(size_t(max_window)),
                                        std::vector<cv::Mat>(size_t(max_window))};
        struct Decoded { std::string error; int frames = 0; double warm_ms = 0; };
        vision::VideoFrame g;
        bool have_g = false;
        const auto decode = [&](const Chunk& c, std::vector<cv::Mat>& dst) {
            Decoded d;
            const int64_t target = times[c.lo];
            // Frames before lo with the same millisecond (a stream with equal
            // timestamps): the landing is on the first of them, lo is the
            // k-th after it.
            size_t k = 0;
            while (k < c.lo && times[c.lo - 1 - k] == target) ++k;
            // The first run of equal times is special: a seek to the first key
            // can drop frames of the run it lands in (OpenCV counts frames by
            // time), so after a seek the stream does not start with the same
            // frames as the forward run saw. Only a reader opened and sought
            // the way the forward run's was sees them again.
            const bool first_run = c.lo == k;
            // Whether the wanted frame has a neighbour of the same time, before or
            // after it: then a landing on the time can be inside the run.
            const bool in_run = k > 0 || (c.lo + 1 < n && times[c.lo + 1] == target);
            // The first seek goes to the time before the run (when there is one),
            // so that it lands before the run and not in it.
            const int64_t first_at = in_run && !first_run ? times[c.lo - k - 1] : target;
            int64_t margin = first_run ? target - from_ms : 0;
            vision::FrameReader* rd = reader.get();
            std::unique_ptr<vision::FrameReader> replay;  // the forward run's own start, again
            for (bool found = false; !found;) {
                const bool last_try = first_run || margin >= target - from_ms;
                const int64_t at = margin == 0 ? first_at : target - margin;
                const auto t0 = Clock::now();
                if (last_try) {
                    // Last resort and the first run: decode from the first key on a
                    // fresh reader, frame for frame what the forward run read.
                    replay = vision::FrameReader::Open(video, &d.error);
                    if (!replay || !replay->SeekMs(from_ms)) {
                        if (d.error.empty()) d.error = "cannot seek in the video";
                        return d;
                    }
                    rd = replay.get();
                } else if (!rd->SeekMs(at)) {
                    d.error = "cannot seek in the video";
                    return d;
                }
                bool saw_before = false;  // a frame earlier than the target came first
                for (;;) {
                    int64_t pts_us = 0;
                    if (!rd->Grab(&pts_us)) {
                        d.error = "the video changed between two reads";
                        return d;
                    }
                    ++d.frames;
                    const int64_t ms = pts_us / 1000;
                    if (ms < target) {  // the warm-up after the seek: grabbed, not converted
                        saw_before = true;
                        continue;
                    }
                    // In a run of equal times only a landing after an earlier frame
                    // (or the replay of the forward run) is known to be on the first
                    // of the run, not inside it.
                    if (ms == target && (!in_run || saw_before || last_try)) {
                        found = true;
                        d.warm_ms = ms_since(t0);
                    }
                    break;
                }
                if (found) break;
                // Landed after the frame, or inside a run of equal times: try
                // again from further back.
                if (last_try) {
                    d.error = "the video changed between two reads";
                    return d;
                }
                margin = margin == 0 ? 2000 : margin * 4;
            }
            for (size_t j = 0; j < k; ++j) {  // step over the equal-time frames before lo
                int64_t pts_us = 0;
                if (!rd->Grab(&pts_us) || pts_us / 1000 != target) {
                    d.error = "the video changed between two reads";
                    return d;
                }
                ++d.frames;
            }
            for (size_t i = c.lo; i < c.hi; ++i) {
                if (i > c.lo) {
                    int64_t pts_us = 0;
                    if (!rd->Grab(&pts_us) || pts_us / 1000 != times[i]) {
                        d.error = "the video changed between two reads";
                        return d;
                    }
                    ++d.frames;
                }
                if (!rd->Retrieve(g)) {
                    d.error = "the video changed between two reads";
                    return d;
                }
                if (!have_g) {
                    have_g = true;
                    note_held();
                }
                shrink(g, dst[i - c.lo]);
            }
            return d;
        };
        std::vector<std::optional<LabelBox>> at(n);
        bool lost = false;
        int window = base_window;
        double warm_sum_ms = 0;
        int warm_n = 0;
        st.window_used = window;
        size_t hi_next = n;
        const auto take = [&] {
            const Chunk c{hi_next > size_t(window) ? hi_next - size_t(window) : 0, hi_next};
            hi_next = c.lo;
            return c;
        };
        Chunk cur = take();
        auto pending = std::async(std::launch::async, decode, cur, std::ref(bufs[0]));
        for (size_t c = 0;; ++c) {
            const Decoded d = pending.get();
            st.frames_decoded += d.frames;
            if (!d.error.empty()) {
                if (error) *error = d.error;
                return std::nullopt;
            }
            if (updates > 0 && update_ms > 0) {
                // What a seek costs the run: the mean seek time of the chunks so
                // far, spread over a chunk, against the time to track a frame.
                // Short GOP: a few percent of the tracking, the window stays.
                // Long GOP: a seek runs through up to a GOP of frames, and the
                // window grows (to max_window_frames) until that is about 5 %.
                warm_sum_ms += d.warm_ms;
                ++warm_n;
                if (warm_n >= 2) {
                    const double track_ms = update_ms / updates;
                    const double want = (warm_sum_ms / warm_n) / (0.05 * track_ms);
                    window = std::clamp(std::max(window, int(std::min(want, double(max_window)))), base_window,
                                        max_window);
                }
                st.window_used = std::max(st.window_used, window);
            }
            const Chunk now = cur;
            if (hi_next > 0) {
                cur = take();
                pending = std::async(std::launch::async, decode, cur, std::ref(bufs[(c + 1) & 1]));
            }
            const std::vector<cv::Mat>& frames = bufs[c & 1];
            for (size_t i = now.hi; i-- > now.lo;) {
                const cv::Mat& frame = frames[i - now.lo];
                if (i == n - 1) {
                    tracker->Init(frame, ToRect(to_box, scale, frame.size()));
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
            if (lost || now.lo == 0) break;
        }
        for (const size_t i : samples) out.backward.push_back({times[i], at[i]});
    }
    if (o.progress) o.progress(100);
    return out;
}

}  // namespace samn::engine
