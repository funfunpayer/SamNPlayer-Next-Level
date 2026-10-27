#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <opencv2/imgproc.hpp>

#include "samn/engine/repair.h"
#include "samn/engine/track_box.h"
#include "synthetic_clips.h"

// Self-check SC-5: failed windows are tracked again from Find tip, and a
// retry is kept only where it passes the checks and still looks like the
// original region; otherwise the window stays a gap.

using namespace samn::engine;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("samn_repair_" + std::to_string(std::rand()) + "_" +
                                            std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// The disc strokes at x 90; from 4 s to 6 s a grey panel covers the left
// half, and when it lifts the disc strokes at x 230. CSRT stays on the left.
bool WriteMovedBehindPanel(const fs::path& p, int seconds) {
    auto w = clips::Writer(p);
    if (!w.isOpened()) return false;
    const cv::Mat bg = clips::Texture(clips::kW, clips::kH, 11, 140);
    for (int i = 0; i < int(clips::kFps * seconds); ++i) {
        const double t = i / clips::kFps;
        cv::Mat f = bg.clone();
        const int y = int(120 + 30 * std::sin(2 * clips::kPi * t));
        clips::Disc(f, t < 5 ? 90 : 230, y);
        if (t >= 4 && t < 6) cv::rectangle(f, cv::Rect(0, 0, 160, clips::kH), cv::Scalar(90, 90, 90), cv::FILLED);
        w.write(f);
    }
    return true;
}

VerifyOptions At25() {
    VerifyOptions v;
    v.window_frames = 50;  // 2 s at 25 fps
    v.skip_frames = 25;
    return v;
}

}  // namespace

TEST_CASE("repair: a failed stretch is tracked again and kept only where it verifies") {
    TempDir dir;
    const auto clip = dir.path / "moved.mp4";
    REQUIRE(WriteMovedBehindPanel(clip, 14));
    const cv::Rect box(55, 85, 70, 70);  // tight around the disc at x 90, y 120

    TrackOptions t;
    t.record_checks = true;
    t.axis = TrackAxis::kY;  // the stroke is vertical; the jump would pull the automatic choice to x
    t.appearance_memory = false;  // a plain CSRT first run, which stays on the left (as on the Owner clip)
    std::string error;
    const auto run = TrackBox(clip, box, t, &error);
    REQUIRE_MESSAGE(run, error);

    RepairOptions o;
    o.track = t;
    o.seed_box = box;
    o.verify = At25();
    const auto r = RepairTrack(clip, *run, o, &error);
    REQUIRE_MESSAGE(r, error);
    REQUIRE(r->positions.size() == run->positions.size());

    // The first run fails after the panel; the repair finds the disc again.
    REQUIRE(r->failed >= 3);
    CHECK(r->repaired >= 2);
    for (const auto& w : r->windows) {
        INFO("window " << w.first << "-" << w.end << std::string(w.repaired ? " repaired" : " gap"));
        if (w.first >= 7 * 25) CHECK(w.repaired);  // well after the panel: the disc is visible again
        if (w.repaired) CHECK(std::abs(w.box.x + w.box.width / 2 - 230) <= w.box.width / 2 + 35);  // the box touches the disc
    }
    // Repaired samples follow the stroke again; gaps are NaN, never invented.
    int repaired_samples = 0;
    for (const auto& w : r->windows) {
        if (!w.repaired) {
            for (int i = w.first; i < w.end; ++i) CHECK(std::isnan(r->positions[size_t(i)]));
            continue;
        }
        double lo = 1e9, hi = -1e9;
        for (int i = w.first; i < w.end; ++i) {
            lo = std::min(lo, r->positions[size_t(i)]);
            hi = std::max(hi, r->positions[size_t(i)]);
            ++repaired_samples;
        }
        CHECK(hi - lo > 30);  // a 60 px stroke, not a flat line
    }
    CHECK(repaired_samples > 0);
    CHECK(!r->run_starts.empty());
}

TEST_CASE("repair: fails closed when no retry looks like the original") {
    // Same clip, but the original look given to the repair is a patch of
    // background: the disc Find tip proposes does not resemble it, so
    // nothing is spliced in and every failed window stays a gap.
    TempDir dir;
    const auto clip = dir.path / "moved.mp4";
    REQUIRE(WriteMovedBehindPanel(clip, 14));
    TrackOptions t;
    t.record_checks = true;
    t.axis = TrackAxis::kY;  // the stroke is vertical; the jump would pull the automatic choice to x
    t.appearance_memory = false;  // a plain CSRT first run, which stays on the left (as on the Owner clip)
    std::string error;
    const auto run = TrackBox(clip, cv::Rect(55, 85, 70, 70), t, &error);
    REQUIRE_MESSAGE(run, error);

    RepairOptions o;
    o.track = t;
    o.seed_box = cv::Rect(250, 10, 50, 50);
    o.verify = At25();
    const auto r = RepairTrack(clip, *run, o, &error);
    REQUIRE_MESSAGE(r, error);
    REQUIRE(r->failed >= 1);
    CHECK(r->repaired == 0);
    CHECK(r->gaps == r->failed);
}
