// ResolveTipForCreate: several start times, and candidate #2 when #1 is weak.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "samn/engine/tip_find.h"
#include "samn/engine/tip_resolve.h"
#include "synthetic_clips.h"

namespace fs = std::filesystem;
using samn::engine::BestUsableTipIndex;
using samn::engine::FindTip;
using samn::engine::kStrongTipScore;
using samn::engine::kWeakTipScore;
using samn::engine::ResolveTipForCreate;
using samn::engine::TipCandidate;
using samn::engine::TipFindOptions;
using samn::engine::TipResolveOptions;
using samn::engine::TipResolveStartMs;
namespace clips = samn::engine::testclips;

namespace {
fs::path Dir(const char* name) {
    const fs::path d = samn::engine::testclips::TempPath(name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
}  // namespace

TEST_CASE("tip resolve: start times are 0, 10, 25, 50, 75 percent, then preview") {
    CHECK(TipResolveStartMs(0) == std::vector<int64_t>{0});
    CHECK(TipResolveStartMs(8000) == std::vector<int64_t>({0, 800, 2000, 4000, 6000}));
    CHECK(TipResolveStartMs(8000, 800) == std::vector<int64_t>({0, 800, 2000, 4000, 6000}));  // preview = 10 %
    CHECK(TipResolveStartMs(8000, 7000) == std::vector<int64_t>({0, 800, 2000, 4000, 6000, 7000}));
    CHECK(TipResolveStartMs(8000, 0) == std::vector<int64_t>({0, 800, 2000, 4000, 6000}));
    CHECK(TipResolveStartMs(8000, 8000).back() != 8000);  // not at/past the end
}

TEST_CASE("tip resolve: candidate #2 wins when #1 is weak") {
    const cv::Rect a(10, 10, 40, 40), b(80, 40, 50, 50), c(200, 10, 30, 30);
    const std::vector<TipCandidate> mixed{{a, 0.02}, {b, 0.20}, {c, 0.01}};
    const auto i = BestUsableTipIndex(mixed, 6);
    REQUIRE(i.has_value());
    CHECK(*i == 1);
    CHECK(mixed[size_t(*i)].box == b);

    CHECK(!BestUsableTipIndex(mixed, 1));  // only #1, below the gate
    CHECK(!BestUsableTipIndex({{a, 0.02}, {c, 0.01}}, 6));
    CHECK(!BestUsableTipIndex({}, 6));

    const std::vector<TipCandidate> sorted{{b, 0.20}, {a, 0.02}};
    CHECK(BestUsableTipIndex(sorted, 6) == 0);
}

TEST_CASE("tip resolve: a later window wins when t=0 is only still") {
    const fs::path dir = Dir("samn_tip_resolve");
    REQUIRE(clips::WriteStillThenMoving(dir / "intro.mp4", 4, 4, 80.0));

    TipFindOptions fo;
    fo.max_seconds = 2;
    fo.start_ms = 0;
    std::string error;
    const auto at0 = FindTip(dir / "intro.mp4", fo, &error);
    REQUIRE_MESSAGE(at0.has_value(), error);
    REQUIRE(!at0->candidates.empty());
    CHECK(at0->candidates.front().score < kWeakTipScore);

    TipResolveOptions o;
    o.max_seconds = 2;
    int last = -1;
    o.progress = [&](int p) { last = p; };
    const auto r = ResolveTipForCreate(dir / "intro.mp4", o, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    CHECK(r->score >= kWeakTipScore);
    CHECK(r->start_ms >= 2000);  // not the still intro
    CHECK(r->candidate_index >= 0);
    CHECK(last == 99);
    const cv::Point centre(r->box.x + r->box.width / 2, r->box.y + r->box.height / 2);
    CHECK_MESSAGE(std::abs(centre.x - 160) <= 50, "box " << r->box);
    fs::remove_all(dir);
}

TEST_CASE("tip resolve: stronger later window beats a marginal early one") {
    const fs::path dir = Dir("samn_tip_resolve_best");
    // Quiet amp ~10 is usually above kWeakTipScore but below kStrongTipScore;
    // amp 80 is a clear stroke. Short windows keep the starts from overlapping.
    REQUIRE(clips::WriteQuietThenStrong(dir / "quiet.mp4", 3, 3, 10.0, 80.0));

    TipFindOptions fo;
    fo.max_seconds = 2;
    fo.start_ms = 0;
    std::string error;
    const auto early = FindTip(dir / "quiet.mp4", fo, &error);
    REQUIRE_MESSAGE(early.has_value(), error);
    REQUIRE(!early->candidates.empty());
    const double early_score = early->candidates.front().score;
    CHECK(early_score >= kWeakTipScore);
    CHECK(early_score < kStrongTipScore);

    fo.start_ms = 3000;
    const auto late = FindTip(dir / "quiet.mp4", fo, &error);
    REQUIRE_MESSAGE(late.has_value(), error);
    REQUIRE(!late->candidates.empty());
    CHECK(late->candidates.front().score > early_score);
    CHECK(late->candidates.front().score >= kStrongTipScore);

    TipResolveOptions o;
    o.max_seconds = 2;
    const auto r = ResolveTipForCreate(dir / "quiet.mp4", o, &error);
    REQUIRE_MESSAGE(r.has_value(), error);
    CHECK(r->start_ms >= 3000);
    CHECK(r->score >= kStrongTipScore);
    CHECK(r->score > early_score);
    fs::remove_all(dir);
}

TEST_CASE("tip resolve: still clip and missing fail closed") {
    const fs::path dir = Dir("samn_tip_resolve_bad");
    REQUIRE(clips::WriteMoving(dir / "still.mp4", 4, 0.0, 1.0));
    std::string error;
    TipResolveOptions o;
    o.max_seconds = 2;
    CHECK(!ResolveTipForCreate(dir / "still.mp4", o, &error));
    CHECK(error.find("weak") != std::string::npos);

    CHECK(!ResolveTipForCreate(dir / "missing.mp4", o, &error));
    CHECK(!error.empty());
    CHECK(error != "canceled");

    REQUIRE(clips::WriteMoving(dir / "c.mp4", 6, 80.0, 1.0));
    int polls = 0;
    o.cancel = [&] { return ++polls > 3; };
    o.max_seconds = 45;
    CHECK(!ResolveTipForCreate(dir / "c.mp4", o, &error));
    CHECK(error == "canceled");
    fs::remove_all(dir);
}
