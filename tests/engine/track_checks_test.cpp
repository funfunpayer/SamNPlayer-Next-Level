#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "samn/engine/self_check.h"
#include "samn/engine/track_box.h"
#include "synthetic_clips.h"

// Self-check SC-2: TrackBox records the checker signals per frame, and the
// verifier reads them (docs/P3_SELF_CHECK_ENGINE.md).

using namespace samn::engine;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("samn_checks_" + std::to_string(std::rand()) + "_" +
                                            std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

TrackResult Track(const fs::path& clip, const cv::Rect& box, const TrackOptions& o) {
    std::string error;
    auto r = TrackBox(clip, box, o, &error);
    REQUIRE_MESSAGE(r, error);
    return *r;
}

VerifyOptions At25(const TrackResult& r) {
    VerifyOptions v;
    v.window_frames = 50;  // 2 s at 25 fps
    v.skip_frames = 25;
    v.cuts = r.scene_cuts;
    return v;
}

int Count(const std::vector<WindowVerdict>& v, WindowState s) {
    int n = 0;
    for (const auto& w : v) n += w.state == s ? 1 : 0;
    return n;
}

const cv::Rect kDiscBox(110, 50, 100, 140);

}  // namespace

TEST_CASE("checks: recording leaves the positions bit-identical") {
    TempDir dir;
    const auto clip = dir.path / "moving.mp4";
    REQUIRE(clips::WriteMoving(clip, 8, 60, 1.0));
    TrackOptions off;
    TrackOptions on;
    on.record_checks = true;
    const auto a = Track(clip, kDiscBox, off);
    const auto b = Track(clip, kDiscBox, on);
    CHECK(a.positions == b.positions);
    CHECK(a.scene_cuts == b.scene_cuts);
    CHECK(a.checks.tracker_ok.empty());

    const size_t n = b.positions.size();
    CHECK(b.checks.box_flow.size() == n);
    CHECK(b.checks.foreground.size() == n);
    CHECK(b.checks.scene_motion.size() == n);
    CHECK(b.checks.tracker_ok.size() == n);
    CHECK(b.checks.camera_inliers.size() == n);
    CHECK(b.checks.appearance.size() == n);
    CHECK(b.checks.box_outside.size() == n);
}

TEST_CASE("checks: a box on the stroke passes every window") {
    TempDir dir;
    const auto clip = dir.path / "moving.mp4";
    REQUIRE(clips::WriteMoving(clip, 12, 60, 1.0));
    TrackOptions o;
    o.record_checks = true;
    const auto r = Track(clip, kDiscBox, o);
    const auto v = VerifyWindows(r.positions, r.checks, At25(r));
    REQUIRE(v.size() >= 5);
    for (const auto& w : v) {
        INFO("window at frame " << w.first << ": " << std::string(WindowStateName(w.state)) << " " << w.reasons);
        CHECK(w.state == WindowState::kPass);
        CHECK(w.c1 > 0.8);
        CHECK(w.c9 == doctest::Approx(0.0));
    }
}

TEST_CASE("checks: flow agreement holds under a camera pan (compensated on both sides)") {
    TempDir dir;
    const auto clip = dir.path / "pan.mp4";
    REQUIRE(clips::WritePanning(clip, 12, 60));
    TrackOptions o;
    o.record_checks = true;
    const auto r = Track(clip, kDiscBox, o);
    const auto v = VerifyWindows(r.positions, r.checks, At25(r));
    REQUIRE(v.size() >= 5);
    CHECK(Count(v, WindowState::kFail) == 0);
    for (const auto& w : v) CHECK(w.c6 > 0.4);  // the camera estimate is trusted here
}

TEST_CASE("checks: a box on still background fails, a box on the stroke does not") {
    TempDir dir;
    const auto clip = dir.path / "moving.mp4";
    REQUIRE(clips::WriteMoving(clip, 12, 60, 1.0));
    TrackOptions o;
    o.record_checks = true;
    o.appearance_memory = false;  // the seed's look would confirm "still" on a static box
    const auto wrong = Track(clip, cv::Rect(10, 10, 50, 50), o);
    const auto v = VerifyWindows(wrong.positions, wrong.checks, At25(wrong));
    REQUIRE(v.size() >= 5);
    for (const auto& w : v) {
        INFO("window at frame " << w.first << ": " << std::string(WindowStateName(w.state)) << " " << w.reasons);
        CHECK(w.state == WindowState::kFail);
    }
}
