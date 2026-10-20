// Tracking between two keyframes of a label track (D-155 step 3), on a
// generated clip: a disc moving up and down.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <random>

#include "samn/engine/label_tracking.h"
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
