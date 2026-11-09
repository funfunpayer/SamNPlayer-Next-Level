// Tracking between two keyframes of a label track (D-155 step 3), on a
// generated clip: a disc moving up and down.
#include <doctest/doctest.h>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <random>
#include <opencv2/imgproc.hpp>

#include "samn/engine/label_tracking.h"
#include "samn/vision/frame_reader.h"
#include "samn/vision/tracker.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using namespace samn::engine::testclips;

namespace {
// The disc's box at ms (WriteMoving: x 160, y = 120 + amp/2 sin(2 pi f t), r 35).
samn::LabelBox DiscAt(int64_t ms, double amp, double freq) {
    const double y = 120 + amp / 2 * std::sin(2 * kPi * freq * double(ms) / 1000.0);
    return {160 - 35, y - 35, 70, 70};
}
}  // namespace

TEST_CASE("tracking between keys follows the disc both ways and blends") {
    const fs::path p = fs::temp_directory_path() / ("samn_between_" + std::to_string(std::random_device{}()) + ".mp4");
    REQUIRE(WriteMoving(p, 3, 60.0, 0.5));
    samn::engine::BetweenKeysOptions o;
    o.step_ms = 200;
    int last = -1;
    o.progress = [&](int v) { last = v; };
    std::string error;
    const auto r = samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), o, &error);
    INFO(error);
    REQUIRE(r);
    CHECK(last == 100);
    REQUIRE(r->forward.size() >= 8);
    REQUIRE(r->forward.size() == r->backward.size());
    const auto keys = samn::BlendTracks(r->forward, r->backward, 0, 2000);
    REQUIRE(keys.size() >= 8);
    for (const auto& k : keys) {
        INFO(k.ms);
        CHECK_FALSE(k.uncertain);
        CHECK(samn::BoxIoU(k.box, DiscAt(k.ms, 60, 0.5)) > 0.6);
    }
    // Errors say why; cancel stops.
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 2000, DiscAt(0, 60, 0.5), 1000, DiscAt(0, 60, 0.5), o, &error));
    samn::engine::BetweenKeysOptions stop;
    stop.cancel = [] { return true; };
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), stop, &error));
    CHECK(error == "canceled");
    samn::engine::BetweenKeysOptions tiny;
    tiny.max_frames = 5;
    CHECK_FALSE(samn::engine::TrackBetweenKeys(p, 0, DiscAt(0, 60, 0.5), 2000, DiscAt(2000, 60, 0.5), tiny, &error));
    CHECK(error.find("too long") != std::string::npos);
    fs::remove(p);
}

// Review E2: a step of zero (or less) made the sample loop run forever. It is
// refused before the video is even opened.
TEST_CASE("tracking between keys refuses a step of zero or less") {
    for (const int64_t step : {int64_t(0), int64_t(-200)}) {
        samn::engine::BetweenKeysOptions o;
        o.step_ms = step;
        std::string error;
        CHECK_FALSE(samn::engine::TrackBetweenKeys("no-such-clip.mp4", 0, DiscAt(0, 60, 0.5), 2000,
                                                   DiscAt(2000, 60, 0.5), o, &error));
        CHECK(error.find("step") != std::string::npos);
    }
}

namespace {

// The tracking as it was before the bounded window (D-155 step 3): every small
// frame of the stretch in memory, both runs over the vector. The reference the
// streaming version must equal bit for bit.
samn::engine::BetweenKeysResult HoldEverything(const fs::path& video, int64_t from_ms, const samn::LabelBox& from_box,
                                               int64_t to_ms, const samn::LabelBox& to_box, int64_t step_ms,
                                               int max_side) {
    std::string error;
    auto reader = samn::vision::FrameReader::Open(video, &error);
    REQUIRE(reader);
    REQUIRE(reader->SeekMs(from_ms));
    std::vector<cv::Mat> frames;
    std::vector<int64_t> times;
    double scale = 1.0;
    samn::vision::VideoFrame f;
    while (reader->Read(f)) {
        const int64_t ms = f.pts_us / 1000;
        if (ms > to_ms) break;
        if (frames.empty()) scale = std::min(1.0, double(max_side) / std::max(f.bgr.cols, f.bgr.rows));
        cv::Mat small;
        if (scale < 1.0) cv::resize(f.bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
        else small = f.bgr.clone();
        frames.push_back(std::move(small));
        times.push_back(ms);
    }
    std::vector<size_t> samples;
    for (int64_t next = from_ms + step_ms; next < to_ms; next += step_ms) {
        const auto it = std::lower_bound(times.begin(), times.end(), next);
        if (it == times.end()) break;
        const size_t i = size_t(it - times.begin());
        if (samples.empty() || samples.back() != i) samples.push_back(i);
    }
    samn::engine::BetweenKeysResult out;
    for (const bool forward : {true, false}) {
        auto tracker = samn::vision::CreateTracker(samn::vision::TrackerKind::kCsrt, &error);
        REQUIRE(tracker);
        const size_t n = frames.size();
        const size_t start = forward ? 0 : n - 1;
        const samn::LabelBox& b = forward ? from_box : to_box;
        cv::Rect init(int(std::lround(b.x * scale)), int(std::lround(b.y * scale)), int(std::lround(b.w * scale)),
                      int(std::lround(b.h * scale)));
        init &= cv::Rect(0, 0, frames[start].cols, frames[start].rows);
        tracker->Init(frames[start], init);
        std::vector<std::optional<samn::LabelBox>> at(n);
        at[start] = b;
        bool lost = false;
        for (size_t k = 1; k < n; ++k) {
            const size_t i = forward ? k : n - 1 - k;
            if (lost) continue;
            if (auto r = tracker->Update(frames[i]))
                at[i] = samn::LabelBox{r->x / scale, r->y / scale, r->width / scale, r->height / scale};
            else lost = true;
        }
        auto& dst = forward ? out.forward : out.backward;
        for (const size_t i : samples) dst.push_back({times[i], at[i]});
    }
    return out;
}

void CheckSameRun(const std::vector<samn::TrackSample>& a, const std::vector<samn::TrackSample>& b) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].ms == b[i].ms);
        REQUIRE(a[i].box.has_value() == b[i].box.has_value());
        if (!a[i].box) continue;
        CHECK(a[i].box->x == b[i].box->x);  // bit for bit: same frames in, same tracker
        CHECK(a[i].box->y == b[i].box->y);
        CHECK(a[i].box->w == b[i].box->w);
        CHECK(a[i].box->h == b[i].box->h);
    }
}

}  // namespace

// The memory fix: the stretch is read through a bounded window, never held
// whole, and the tracked boxes are exactly those of the hold-everything
// version, for a window larger than the stretch, a few chunks, and one frame.
TEST_CASE("tracking between keys holds a bounded window and gives the same boxes") {
    const fs::path p = TempPath("samn_between_window") .replace_extension(".mp4");
    REQUIRE(WriteMoving(p, 4, 60.0, 0.5));  // 100 frames
    const samn::LabelBox a = DiscAt(0, 60, 0.5), b = DiscAt(3000, 60, 0.5);
    const auto ref = HoldEverything(p, 0, a, 3000, b, 200, 640);
    REQUIRE(ref.forward.size() >= 10);
    for (const int window : {1, 7, 24, 500}) {
        INFO("window " << window);
        samn::engine::BetweenKeysOptions o;
        o.window_frames = window;
        samn::engine::BetweenKeysStats st;
        o.stats = &st;
        std::string error;
        const auto r = samn::engine::TrackBetweenKeys(p, 0, a, 3000, b, o, &error);
        INFO(error);
        REQUIRE(r);
        CheckSameRun(r->forward, ref.forward);
        CheckSameRun(r->backward, ref.backward);
        CHECK(st.peak_frames_held >= 1);
        CHECK(st.peak_frames_held <= 2 * window);  // the chunk in use and the one being decoded
        if (window < 38) CHECK(st.peak_frames_held < 76);  // 76 frames in the stretch: never all of them
        // Both runs read every frame once; the backward chunks add no more.
        CHECK(st.frames_decoded >= 2 * 75);
        CHECK(st.frames_decoded <= 2 * 76 + 1);
    }
    fs::remove(p);
}
