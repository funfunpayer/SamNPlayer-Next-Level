#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include "samn/engine/compare.h"
#include "samn/funscript.h"
#include "synthetic_clips.h"

using namespace samn::engine;
using samn::Action;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {
fs::path TempDir(const char* name) {
    const fs::path p = fs::temp_directory_path() / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// The disc's true motion as a script: up on screen = high position.
std::vector<Action> Truth(int seconds, int64_t shift_ms) {
    std::vector<Action> out;
    for (int t = 0; t <= seconds * 1000; t += 40)
        out.push_back({t + shift_ms, int(std::lround(50 - 45 * std::sin(2 * clips::kPi * t / 1000.0)))});
    return out;
}

void WriteScript(const fs::path& p, const std::vector<Action>& a) {
    samn::Script s;
    s.actions = a;
    std::ofstream(p) << samn::Write(s);
}
}  // namespace

TEST_CASE("compare: a pair score carries windows and flags") {
    std::vector<Action> ref, cand;
    for (int t = 0; t <= 40000; t += 50) {
        // Non-periodic so the lag is unambiguous.
        const double f = 0.0004 + 0.0000008 * t;
        const int pos = int(std::lround(50 + 35 * std::sin(2 * clips::kPi * f * t)));
        ref.push_back({t, pos});
        cand.push_back({t + 300, pos});
    }
    CompareSettings s;
    const PairScore p = ScorePair(ref, cand, s);
    REQUIRE(p.whole);
    CHECK(p.whole->lag_ms == -300);
    CHECK(!p.boundary_pinned);
    CHECK(p.windows.size() == 4);
    CHECK(p.windows_scored == 4);
    REQUIRE(p.windowed_r_median);
    CHECK(*p.windowed_r_median > 0.95);

    // A shift beyond the window pins the lag to its edge: untrusted (GD-05).
    for (Action& a : cand) a.at += 1500;
    const PairScore pinned = ScorePair(ref, cand, s);
    REQUIRE(pinned.whole);
    CHECK(pinned.boundary_pinned);
}

TEST_CASE("compare: the manifest is checked and resolved") {
    const fs::path dir = TempDir("samn_compare_manifest");
    CompareSettings s;
    std::string error;
    std::ofstream(dir / "bad.json") << R"({"clips": [{"video": "a.mp4", "box": [1, 2, 3]}]})";
    CHECK(!LoadManifest(dir / "bad.json", &s, &error));
    CHECK(error.find("box") != std::string::npos);
    std::ofstream(dir / "empty.json") << R"({"clips": [{"video": "a.mp4", "box": [1, 2, 0, 5]}]})";
    CHECK(!LoadManifest(dir / "empty.json", &s, &error));
    CHECK(!LoadManifest(dir / "missing.json", &s, &error));

    std::ofstream(dir / "ok.json") << R"({"window_ms": 5000, "clips": [{"name": "c1", "video": "v/a.mp4",
        "box": [1, 2, 3, 4], "profile": "autotune", "reference": "r.funscript", "old": "/abs/o.funscript"}]})";
    const auto clips_ = LoadManifest(dir / "ok.json", &s, &error);
    REQUIRE_MESSAGE(clips_, error);
    REQUIRE(clips_->size() == 1);
    const CompareClip& c = clips_->front();
    CHECK(c.name == "c1");
    CHECK(c.video == dir / "v/a.mp4");
    CHECK(c.box == cv::Rect(1, 2, 3, 4));
    CHECK(c.signal.profile == "autotune");
    CHECK(c.reference == dir / "r.funscript");
    CHECK(c.old_output == fs::path("/abs/o.funscript"));
    CHECK(s.window_ms == 5000);
    fs::remove_all(dir);
}

TEST_CASE("compare: a clip is tracked and scored against reference and 0.7") {
    const fs::path dir = TempDir("samn_compare_clip");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 8, 80.0, 1.0));
    WriteScript(dir / "truth.funscript", Truth(8, 0));
    WriteScript(dir / "old.funscript", Truth(8, 200));  // a 0.7 stand-in, 200 ms late

    CompareClip clip;
    clip.name = "moving";
    clip.video = dir / "moving.mp4";
    clip.box = cv::Rect(100, 40, 120, 160);
    clip.signal.profile = "standard";
    clip.reference = dir / "truth.funscript";
    clip.old_output = dir / "old.funscript";
    CompareSettings s;
    s.window_ms = 4000;
    const ClipReport r = RunClip(clip, s);
    REQUIRE_MESSAGE(r.error.empty(), r.error);
    CHECK(r.frames == 200);
    CHECK(r.video_sha256.size() == 64);
    CHECK(r.reference_sha256.size() == 64);
    CHECK(!r.new_actions.empty());
    REQUIRE(r.new_vs_reference);
    REQUIRE(r.new_vs_reference->whole);
    CHECK(r.new_vs_reference->whole->r > 0.9);
    CHECK(r.new_vs_reference->whole->orientation == "normal");
    REQUIRE(r.old_vs_reference);
    REQUIRE(r.old_vs_reference->whole);
    CHECK(std::abs(r.old_vs_reference->whole->lag_ms + 200) <= 100);
    REQUIRE(r.new_vs_old);

    const auto j = ReportJson({r}, s);
    CHECK(j["clips"][0]["name"] == "moving");
    CHECK(j["clips"][0]["new_vs_reference"]["windows"].size() == 2);
    const std::string md = ReportMarkdown({r}, s);
    CHECK(md.find("| moving | standard | new vs reference |") != std::string::npos);
    CHECK(md.find("| moving | standard | 0.7 vs reference |") != std::string::npos);
    CHECK(md.find("video `" + r.video_sha256.substr(0, 16)) != std::string::npos);

    // A missing video is an error row, not a crash or a silent zero.
    clip.video = dir / "gone.mp4";
    const ClipReport bad = RunClip(clip, s);
    CHECK(!bad.error.empty());
    CHECK(ReportMarkdown({bad}, s).find("error:") != std::string::npos);
    fs::remove_all(dir);
}
