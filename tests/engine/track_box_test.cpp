#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

#include <opencv2/imgproc.hpp>

#include "samn/engine/track_box.h"
#include "synthetic_clips.h"
#include "track_parts.h"

using namespace samn::engine;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("samn_engine_" + std::to_string(std::rand()) + "_" +
                                            std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

double Ptp(const std::vector<double>& v) {
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return *hi - *lo;
}

const cv::Rect kRoi(100, 40, 120, 160);

TrackOptions Plain(TrackAxis axis = TrackAxis::kY) {
    TrackOptions o;
    o.camera_compensation = false;
    o.scene_cuts = false;
    o.appearance_memory = false;
    o.axis = axis;
    return o;
}

TrackResult Track(const fs::path& clip, const TrackOptions& o) {
    std::string error;
    auto r = TrackBox(clip, kRoi, o, &error);
    REQUIRE_MESSAGE(r, error);
    return *r;
}

}  // namespace

// Ported from 0.7 trackcv/track_test.go (same clips, same bounds).
TEST_CASE("track: follows the amplitude on easy material") {
    TempDir dir;
    const fs::path clip = dir.path / "moving.mp4";
    REQUIRE(clips::WriteMoving(clip, 8, 80.0, 1.0));
    const TrackResult r = Track(clip, Plain());
    CHECK(r.times_ms.size() == r.positions.size());
    CHECK(r.size == cv::Size(clips::kW, clips::kH));
    CHECK(std::abs(Ptp(r.positions) - 80.0) <= 25);
    CHECK(r.stats.lost_frames <= r.stats.total_frames / 20);
    CHECK(r.stats.total_frames == 200);
    CHECK(r.stats.reason.empty());
    // Video time per frame: 40 ms apart at 25 fps.
    CHECK(r.times_ms.front() == 0);
    CHECK(r.times_ms[25] == 1000);
}

TEST_CASE("track: camera compensation brings the amplitude closer") {
    TempDir dir;
    const fs::path clip = dir.path / "panning.mp4";
    REQUIRE(clips::WritePanning(clip, 10, 80.0));
    const TrackResult raw = Track(clip, Plain());
    TrackOptions o = Plain();
    o.camera_compensation = true;
    const TrackResult comp = Track(clip, o);
    const double raw_err = std::abs(Ptp(raw.positions) - 80.0);
    const double comp_err = std::abs(Ptp(comp.positions) - 80.0);
    CHECK_MESSAGE(comp_err < raw_err, "raw off by " << raw_err << ", compensated off by " << comp_err);
}

TEST_CASE("track: a hard cut is found and motion continues after it") {
    TempDir dir;
    const fs::path clip = dir.path / "cut.mp4";
    REQUIRE(clips::WriteHardCut(clip, 8, 100, 80.0));
    TrackOptions o = Plain();
    o.scene_cuts = true;
    o.appearance_memory = true;
    const TrackResult r = Track(clip, o);
    REQUIRE(!r.scene_cuts.empty());
    CHECK(r.scene_cuts.front() == 100);
    REQUIRE(r.positions.size() > 110);
    CHECK(Ptp(std::vector<double>(r.positions.begin() + 110, r.positions.end())) >= 24);
}

// The memory's crops carry the box's background, so with 0.7's loose box
// (120x160 around a 70 px disc) it practically never reacquires after a cut
// (P3.1 finding, improvement candidate). A tight box shows that reacquire
// itself works.
TEST_CASE("track: after a cut the memory finds the subject at its new place") {
    TempDir dir;
    const fs::path clip = dir.path / "cut_jump.mp4";
    REQUIRE(clips::WriteLightingCutJump(clip, 4, 50));
    TrackOptions o = Plain();
    o.scene_cuts = true;
    o.appearance_memory = true;
    std::string error;
    const auto r = TrackBox(clip, cv::Rect(125, 85, 70, 70), o, &error);
    REQUIRE_MESSAGE(r, error);
    REQUIRE(r->scene_cuts == std::vector<int>{50});
    for (size_t i = 50; i < r->xs.size(); ++i) CHECK(std::abs(r->xs[i] - 280) <= 8);
}

TEST_CASE("track: the gated memory does not lose easy material") {
    TempDir dir;
    const fs::path clip = dir.path / "amem.mp4";
    REQUIRE(clips::WriteMoving(clip, 4, 80.0, 1.0));
    TrackOptions o;
    o.camera_compensation = false;
    o.scene_cuts = false;
    std::string error;
    const auto r = TrackBox(clip, cv::Rect(125, 80, 70, 80), o, &error);
    REQUIRE_MESSAGE(r, error);
    CHECK(r->stats.valid_frames >= r->stats.total_frames / 2);
    CHECK(r->stats.valid_frames == r->stats.total_frames);
}

TEST_CASE("track: the axis follows the motion") {
    TempDir dir;
    const fs::path clip = dir.path / "vertical.mp4";
    REQUIRE(clips::WriteMoving(clip, 8, 80.0, 1.0));
    const TrackResult x = Track(clip, Plain(TrackAxis::kX));
    const TrackResult y = Track(clip, Plain(TrackAxis::kY));
    const TrackResult a = Track(clip, Plain(TrackAxis::kAuto));
    CHECK(Ptp(x.positions) < Ptp(y.positions));
    CHECK(!a.horizontal);
    CHECK(a.positions == y.positions);
}

TEST_CASE("track: max frames, start time, progress and cancel") {
    TempDir dir;
    const fs::path clip = dir.path / "moving.mp4";
    REQUIRE(clips::WriteMoving(clip, 8, 80.0, 1.0));

    TrackOptions o = Plain();
    o.max_frames = 40;
    CHECK(Track(clip, o).stats.total_frames == 40);

    // The clock is video time after a seek (GD-07).
    o = Plain();
    o.start_ms = 2000;
    o.max_frames = 10;
    const TrackResult s = Track(clip, o);
    CHECK(s.times_ms.front() == 2000);
    CHECK(s.times_ms.back() == 2360);

    o = Plain();
    int calls = 0, last_done = -1, last_total = -1;
    o.on_progress = [&](int done, int total) {
        ++calls;
        last_done = done;
        last_total = total;
    };
    Track(clip, o);
    CHECK(calls >= 90);
    CHECK(calls <= 110);
    CHECK(last_total == 200);
    CHECK(last_done == 200);

    o = Plain();
    int frames = 0;
    o.cancel = [&] { return ++frames > 5; };
    CHECK(Track(clip, o).canceled);
}

TEST_CASE("track: a missing file is an error, not an empty track") {
    std::string error;
    CHECK(!TrackBox("no/such/clip.mp4", kRoi, TrackOptions{}, &error));
    CHECK(error.find("not found") != std::string::npos);
}

// ---- building blocks ----

TEST_CASE("parts: savgol keeps a parabola and matches scipy interp at the edges") {
    std::vector<double> y;
    for (int i = 0; i < 20; ++i) y.push_back(0.5 * i * i - 3 * i + 7);
    const std::vector<double> s = Savgol(y, 9, 2);
    for (size_t i = 0; i < y.size(); ++i) CHECK(s[i] == doctest::Approx(y[i]).epsilon(1e-9));
    // Too short: unchanged.
    CHECK(Savgol({1, 5, 2}, 9, 2) == std::vector<double>{1, 5, 2});
    // scipy.signal.savgol_filter([0,0,0,0,9,0,0,0,0,0], 9, 2) (mode interp).
    const std::vector<double> spike = Savgol({0, 0, 0, 0, 9, 0, 0, 0, 0, 0}, 9, 2);
    CHECK(spike[4] == doctest::Approx(9 * 59.0 / 231).epsilon(1e-9));
}

TEST_CASE("parts: camera compensation is smoothed per scene") {
    std::vector<double> ys(30, 100.0), dy(30);
    for (int i = 0; i < 30; ++i) dy[i] = i < 15 ? i : 0;  // drift, then reset by a cut at 15
    ApplyCameraCompensation(ys, dy, {15});
    CHECK(ys[10] == doctest::Approx(90.0));
    CHECK(ys[20] == doctest::Approx(100.0));
}

// Ported from 0.7 dispguard_test.go (real-clip scale: genuine motion up to
// ~42 px a frame, the two jumps 106.8 and 294.5 px).
TEST_CASE("parts: the jump guard flags real-clip jumps, not real motion") {
    const std::vector<double> calm{1.2, 1.5, 1.4, 1.6, 1.3, 1.5, 1.4, 1.5, 1.6, 1.4};
    DispGuard g;
    CHECK(!g.Implausible(500));  // cold start: nothing to compare against
    for (double d : calm) {
        CHECK(!g.Implausible(d));
        g.Accept(d);
    }
    CHECK(g.Implausible(106.8));
    CHECK(g.Implausible(294.5));
    CHECK(!g.Implausible(41.5));

    DispGuard jitter;
    for (int i = 0; i < 20; ++i) jitter.Accept(0.1);
    CHECK(!jitter.Implausible(59));  // under the 60 px floor whatever the ratio
    CHECK(jitter.Implausible(61));

    DispGuard fast;
    for (int i = 0; i < 60; ++i) fast.Accept(i < 40 ? 1.0 : 30.0);  // window keeps the last 20
    CHECK(!fast.Implausible(35));
    CHECK(!fast.Implausible(200));
    CHECK(fast.Implausible(241));
}

TEST_CASE("parts: progress total after a seek (0.7 progress_total_test.go)") {
    CHECK(ProgressTotal(3000, 1500, 0) == 1500);
    CHECK(ProgressTotal(3000, 1500, 200) == 200);
    CHECK(ProgressTotal(3000, 0, 0) == 3000);
    CHECK(ProgressTotal(300, 600, 100) == 0);  // known and exhausted
    CHECK(ProgressTotal(0, 600, 100) == 100);  // unknown, capped
}

// Ported from 0.7 trackutil/coast_test.go (Reset and MaxFrames follow with
// the two-point tracker, P3.4).
TEST_CASE("parts: coast moves on for 8 frames, then gives up") {
    Coast c;
    CHECK(!c.OnLost());
    c = Coast();
    c.ObserveOk({10, 10, 20, 20});
    c.ObserveOk({13, 8, 20, 20});
    for (int i = 1; i <= Coast::kDefaultFrames; ++i) {
        auto r = c.OnLost();
        REQUIRE(r);
        CHECK(r->x == 13 + 3 * i);
        CHECK(r->y == 8 - 2 * i);
        CHECK(r->size() == cv::Size(20, 20));
    }
    CHECK(!c.OnLost());
    // A new observation reopens the budget.
    c.ObserveOk({50, 50, 20, 20});
    CHECK(c.OnLost());

    // One sample only: hold the box, invent no motion.
    Coast one;
    one.ObserveOk({5, 5, 10, 10});
    auto held = one.OnLost();
    REQUIRE(held);
    CHECK(held->tl() == cv::Point(5, 5));
}

TEST_CASE("parts: appearance memory knows the start region and fails closed") {
    cv::Mat bgr = clips::Texture(clips::kW, clips::kH, 11, 140);
    clips::Disc(bgr, 160, 120);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const cv::Rect roi(125, 85, 70, 70);

    AppearanceMemory m;
    CHECK(!m.MatchesOriginal(gray, roi));
    m.Remember(gray, roi);
    auto same = m.MatchesOriginal(gray, roi);
    REQUIRE(same);
    CHECK(*same >= AppearanceMemory::kMinScore);
    auto drifted = m.MatchesOriginal(gray, {0, 0, 20, 20});
    CHECK((!drifted || *drifted < AppearanceMemory::kMinScore));

    // A collapsed box still on the disc is recognised (the window follows
    // the first crop's size), a collapsed box elsewhere is not.
    auto shrunk = m.MatchesOriginal(gray, {152, 112, 16, 16});
    REQUIRE(shrunk);
    CHECK(*shrunk >= AppearanceMemory::kMinScore);
    auto shrunk_away = m.MatchesOriginal(gray, {2, 2, 16, 16});
    CHECK((!shrunk_away || *shrunk_away < AppearanceMemory::kMinScore));

    // Moved disc: found again near its new place.
    cv::Mat moved = clips::Texture(clips::kW, clips::kH, 11, 140);
    clips::Disc(moved, 200, 140);
    cv::cvtColor(moved, gray, cv::COLOR_BGR2GRAY);
    auto found = m.Reacquire(gray);
    REQUIRE(found);
    CHECK(std::abs(found->x + found->width / 2 - 200) <= 6);
    CHECK(std::abs(found->y + found->height / 2 - 140) <= 6);

    // Only background: nothing, rather than a guess.
    gray = cv::Mat(clips::kH, clips::kW, CV_8U, cv::Scalar(40));
    CHECK(!m.Reacquire(gray));
}

TEST_CASE("parts: the memory keeps the first crop and at most 8") {
    cv::Mat gray(240, 320, CV_8U);
    cv::randu(gray, 0, 255);
    AppearanceMemory m;
    for (int i = 0; i < 12; ++i) m.Remember(gray, {10 + i, 10, 40, 40});
    CHECK(m.size() == 8);
    m.Remember(gray, {315, 235, 40, 40});  // too small after clamping: ignored
    CHECK(m.size() == 8);
}

TEST_CASE("parts: scene cut on a new background, none on small motion") {
    cv::Mat a = clips::Texture(clips::kW, clips::kH, 11, 140), b = a.clone(), c;
    clips::Disc(a, 160, 120);
    clips::Disc(b, 160, 126);
    clips::Texture(clips::kW, clips::kH, 99, 140).convertTo(c, -1, 0.4, 12);
    cv::Mat ga, gb, gc;
    cv::cvtColor(a, ga, cv::COLOR_BGR2GRAY);
    cv::cvtColor(b, gb, cv::COLOR_BGR2GRAY);
    cv::cvtColor(c, gc, cv::COLOR_BGR2GRAY);
    CHECK(!IsSceneCut(ga, gb));
    CHECK(IsSceneCut(gb, gc));
}
