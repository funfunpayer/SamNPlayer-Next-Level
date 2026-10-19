// Find tip (0.7 generator/auto_roi.py) on generated clips: a disc moving up
// and down at 1 Hz over a busy picture, with and without the camera
// panning. SAMN_TIP_KEEP_DIR keeps the clips there (for the parity check
// against 0.7's Python in docs/benchmarks/P3_TIP_FIND.md).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "samn/engine/tip_find.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using samn::engine::FindTip;
using samn::engine::PeakRegions;
using samn::engine::PeriodicityScore;
using samn::engine::TipFindOptions;
namespace clips = samn::engine::testclips;

namespace {

fs::path Dir(const char* name) {
    if (const char* keep = std::getenv("SAMN_TIP_KEEP_DIR")) {
        fs::create_directories(keep);
        return keep;
    }
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// The disc (radius 35) sits at x 160 and moves over y 80..160 (+ radius).
void CheckOnTheDisc(const cv::Rect& b) {
    const cv::Point centre(b.x + b.width / 2, b.y + b.height / 2);
    CHECK_MESSAGE(std::abs(centre.x - 160) <= 40, "box " << b);
    CHECK_MESSAGE((centre.y >= 50 && centre.y <= 190), "box " << b);
    CHECK(b.width < 200);  // compact, not the whole frame
    CHECK(b.height < 160);
}

}  // namespace

TEST_CASE("tip find: periodicity prefers one clean rhythm") {
    std::vector<double> sine, noise;
    unsigned seed = 7;
    for (int i = 0; i < 150; ++i) {
        sine.push_back(5 * std::sin(2 * 3.14159265358979 * 1.0 * i / 12.5));  // 1 Hz at 12.5 fps
        seed = seed * 1103515245u + 12345u;
        noise.push_back(5 * (double((seed >> 16) % 1000) / 500.0 - 1.0));
    }
    CHECK(PeriodicityScore(sine, 12.5) > 2 * PeriodicityScore(noise, 12.5));
    CHECK(PeriodicityScore(std::vector<double>(150, 3.0), 12.5) == 0);  // no motion
    CHECK(PeriodicityScore({1, 2, 3}, 12.5) == 0);                      // too short
}

TEST_CASE("tip find: peak regions grow from the strongest cell, best neighbour first") {
    cv::Mat s = cv::Mat::zeros(8, 12, CV_64F);
    s.at<double>(2, 3) = 10;
    s.at<double>(2, 4) = 8;
    s.at<double>(3, 3) = 5;
    s.at<double>(6, 10) = 7;  // a second, separate region
    s.at<double>(0, 0) = 1;   // below 0.4 x 10 next to nothing: its own small region
    const auto regions = PeakRegions(s);
    REQUIRE(regions.size() == 3);
    CHECK(regions[0] == std::vector<std::pair<int, int>>{{2, 3}, {2, 4}, {3, 3}});
    CHECK(regions[1] == std::vector<std::pair<int, int>>{{6, 10}});
    CHECK(regions[2] == std::vector<std::pair<int, int>>{{0, 0}});
}

TEST_CASE("tip find: the moving disc is found") {
    const fs::path dir = Dir("samn_tip_find");
    REQUIRE(clips::WriteMoving(dir / "tip_moving.mp4", 8, 80.0, 1.0));
    int last = -1;
    TipFindOptions o;
    o.progress = [&](int p) { last = p; };
    std::string error;
    const auto r = FindTip(dir / "tip_moving.mp4", o, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    MESSAGE("moving: box " << r->box << ", " << r->candidates.size() << " candidates, best " << r->candidates[0].box);
    CheckOnTheDisc(r->box);
    REQUIRE(!r->candidates.empty());
    CHECK(r->candidates[0].score >= r->candidates.back().score);
    CHECK(last == 99);
}

TEST_CASE("tip find: the disc is found while the camera pans") {
    const fs::path dir = Dir("samn_tip_find_pan");
    REQUIRE(clips::WritePanning(dir / "tip_panning.mp4", 10, 80.0));
    std::string error;
    const auto r = FindTip(dir / "tip_panning.mp4", {}, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    MESSAGE("panning: box " << r->box);
    CheckOnTheDisc(r->box);
}

TEST_CASE("tip find: no rhythm, too short, missing, cancel") {
    const fs::path dir = Dir("samn_tip_find_errors");
    std::string error;
    CHECK(!FindTip(dir / "missing.mp4", {}, &error));
    CHECK(!error.empty());

    // The disc never moves: only compression noise is left. Like 0.7, a
    // (tiny) proposal still comes back, with a score near zero that the page
    // reports as weak.
    REQUIRE(clips::WriteMoving(dir / "still.mp4", 4, 0.0, 1.0));
    const auto still = FindTip(dir / "still.mp4", {}, &error);
    REQUIRE(still.has_value());
    CHECK(still->candidates[0].score < 0.01);

    REQUIRE(clips::WriteMoving(dir / "short.mp4", 0, 80.0, 1.0));
    CHECK(!FindTip(dir / "short.mp4", {}, &error));

    REQUIRE(clips::WriteMoving(dir / "long.mp4", 6, 80.0, 1.0));
    TipFindOptions o;
    int polls = 0;
    o.cancel = [&] { return ++polls > 5; };
    CHECK(!FindTip(dir / "long.mp4", o, &error));
    CHECK(error == "canceled");
}
