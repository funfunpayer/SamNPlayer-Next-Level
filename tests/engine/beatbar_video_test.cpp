// The beat bar on real (generated) video files: decode, find the strip, time
// every beat (0.7 generator/beatbar_video.go). No media in the repo.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

#include "samn/engine/beatbar_video.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using samn::engine::BeatBarOptions;
using samn::engine::DetectBeatBarBeats;
namespace clips = samn::engine::testclips;

namespace {

fs::path Dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void CheckBeats(const std::vector<double>& found, const std::vector<double>& truth) {
    // One frame at 30 fps is 33 ms; a beat may fall between two frames.
    CHECK(found.size() + 1 >= truth.size());
    CHECK(found.size() <= truth.size() + 1);
    for (double t : truth) {
        double best = std::numeric_limits<double>::infinity();
        for (double b : found) best = std::min(best, std::abs(b - t));
        CHECK_MESSAGE(best <= 50, "beat at " << t << " ms missed by " << best << " ms");
    }
}

}  // namespace

TEST_CASE("beat bar video: a bottom strip is found and every beat timed") {
    const fs::path dir = Dir("samn_beatbar_bottom");
    const auto truth = clips::WriteBeatBar(dir / "bar.mp4", 12, false);
    REQUIRE(!truth.empty());
    int last = -1;
    bool backwards = false;
    BeatBarOptions o;
    o.progress = [&](int p) {
        backwards |= p < last;
        last = p;
    };
    std::string error;
    const auto r = DetectBeatBarBeats(dir / "bar.mp4", o, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    REQUIRE(r->found);
    CHECK(!r->vertical);
    CHECK(r->bar.y0 >= 160);
    CHECK(r->bar.y1 <= 184);
    CHECK(r->bar.velocity < 0);
    CHECK(std::abs(r->bar.target_x - 60) <= 4);
    CheckBeats(r->beats_ms, truth);
    CHECK(std::abs(r->bpm - 60000.0 / (17 * 1000.0 / 30)) <= 2);  // one beat every 17 frames
    CHECK(!backwards);
    CHECK(last >= 90);
    fs::remove_all(dir);
}

TEST_CASE("beat bar video: a strip down the side edge is read turned") {
    const fs::path dir = Dir("samn_beatbar_side");
    const auto truth = clips::WriteBeatBar(dir / "side.mp4", 12, true);
    REQUIRE(!truth.empty());
    std::string error;
    const auto r = DetectBeatBarBeats(dir / "side.mp4", {}, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    REQUIRE(r->found);
    CHECK(r->vertical);
    CheckBeats(r->beats_ms, truth);
    fs::remove_all(dir);
}

TEST_CASE("beat bar video: no bar, a missing file, cancel") {
    const fs::path dir = Dir("samn_beatbar_none");
    REQUIRE(clips::WriteMoving(dir / "plain.mp4", 4, 80.0, 1.0));
    std::string error;
    const auto none = DetectBeatBarBeats(dir / "plain.mp4", {}, &error);
    REQUIRE(none.has_value());
    CHECK(!none->found);
    CHECK(none->beats_ms.empty());

    CHECK(!DetectBeatBarBeats(dir / "missing.mp4", {}, &error));
    CHECK(!error.empty());

    REQUIRE(!clips::WriteBeatBar(dir / "bar.mp4", 4, false).empty());
    BeatBarOptions o;
    int frames = 0;
    o.cancel = [&] { return ++frames > 20; };
    error.clear();
    CHECK(!DetectBeatBarBeats(dir / "bar.mp4", o, &error));
    CHECK(error == "canceled");
    fs::remove_all(dir);
}
