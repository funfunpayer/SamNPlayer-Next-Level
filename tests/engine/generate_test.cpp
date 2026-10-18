#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/engine/generate.h"
#include "samn/samn_file.h"
#include "synthetic_clips.h"

using namespace samn::engine;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

fs::path TempDir(const char* name) {
    const fs::path p = fs::temp_directory_path() / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// Collects events from the worker thread.
struct Recorder {
    std::mutex m;
    std::vector<GenerateEvent> events;
    GenerateCallback fn() {
        return [this](const GenerateEvent& e) {
            std::lock_guard<std::mutex> l(m);
            events.push_back(e);
        };
    }
    std::vector<GenerateEvent> take() {
        std::lock_guard<std::mutex> l(m);
        return events;
    }
};

GenerateRequest Request(const fs::path& dir, const char* clip, const char* profile) {
    GenerateRequest r;
    r.video = dir / clip;
    r.box = cv::Rect(100, 40, 120, 160);
    r.signal.profile = profile;
    r.output = dir / (std::string(profile) + ".funscript");
    return r;
}

nlohmann::json ReadJson(const fs::path& p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

}  // namespace

TEST_CASE("generate: writes the funscript and its .samn as 0.7 did") {
    const fs::path dir = TempDir("samn_generate_write");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 8, 80.0, 1.0));  // O-zones need 4 s and more
    Recorder rec;
    Generate(Request(dir, "moving.mp4", "standard"), 7, {}, rec.fn());
    const auto ev = rec.take();
    REQUIRE(!ev.empty());
    CHECK(ev.back().kind == GenerateEventKind::kDone);
    for (const auto& e : ev) CHECK(e.run_id == 7);
    // Progress from the first frame on, and it never runs backwards.
    int progress = 0, last = -1;
    for (const auto& e : ev)
        if (e.kind == GenerateEventKind::kProgress) {
            ++progress;
            CHECK(e.percent >= last);
            last = e.percent;
        }
    CHECK(progress >= 50);
    CHECK(ev.front().kind == GenerateEventKind::kProgress);
    CHECK(ev.front().percent == 0);

    const auto fs_json = ReadJson(dir / "standard.funscript");
    CHECK(fs_json["actions"].size() == ev.back().actions);
    CHECK(fs_json["actions"].size() > 4);
    const auto& np = fs_json["metadata"]["native_pipeline"];
    CHECK(np["frames"] == 200);
    CHECK(np["lost"] == 0);
    CHECK(np["tracking"] == "samn_engine");
    CHECK(!fs_json["metadata"].contains("device_recipe"));
    CHECK(!fs_json["metadata"].contains("quality_score"));  // only once the Quality Doctor runs (P3.6)

    const samn::Document d = samn::LoadDocument(dir / "standard.samn");
    CHECK(d.general.size() == fs_json["actions"].size());
    // The automatic O-markers land in both files, as in 0.7.
    REQUIRE(!d.o_markers.empty());
    CHECK(d.o_markers[0].kind == "primary");
    CHECK(fs_json["metadata"]["oMarkers"].size() == d.o_markers.size());
    bool noted = false;
    for (const auto& e : ev) noted = noted || e.message.rfind("O-marker set", 0) == 0;
    CHECK(noted);
    CHECK(d.video_path == (dir / "moving.mp4").u8string());
    fs::remove_all(dir);
}

TEST_CASE("generate: a distance profile carries the recipe and baked axes") {
    const fs::path dir = TempDir("samn_generate_tj");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 4, 80.0, 1.0));
    Recorder rec;
    Generate(Request(dir, "moving.mp4", "tj"), 1, {}, rec.fn());
    REQUIRE(rec.take().back().kind == GenerateEventKind::kDone);
    const nlohmann::json doc = ReadJson(dir / "tj.funscript");  // kept alive for the loop below
    const auto& meta = doc["metadata"];
    CHECK(meta["profile"] == "tj");
    CHECK(meta.contains("device_recipe"));
    for (const auto& a : doc["actions"]) {
        CHECK(a["pos"].get<int>() >= 20);
        CHECK(a["pos"].get<int>() <= 90);
    }
    const samn::Document d = samn::LoadDocument(dir / "tj.samn");
    CHECK(d.active_strength == "normal");
    CHECK(!d.vibration.empty());
    fs::remove_all(dir);
}

TEST_CASE("generate: never overwrites unless asked") {
    const fs::path dir = TempDir("samn_generate_guard");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 2, 80.0, 1.0));
    std::ofstream(dir / "standard.funscript") << "keep me";
    Recorder rec;
    GenerateRequest req = Request(dir, "moving.mp4", "standard");
    Generate(req, 1, {}, rec.fn());
    REQUIRE(rec.take().size() == 1);
    CHECK(rec.take().back().kind == GenerateEventKind::kError);
    req.auto_ozone = false;
    std::ifstream in(dir / "standard.funscript");
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(ss.str() == "keep me");

    req.overwrite = true;
    Generate(req, 2, {}, rec.fn());
    CHECK(rec.take().back().kind == GenerateEventKind::kDone);
    fs::remove_all(dir);
}

TEST_CASE("generate: errors and cancel are reported, cancel writes nothing") {
    const fs::path dir = TempDir("samn_generate_cancel");
    Recorder rec;
    Generate(Request(dir, "missing.mp4", "standard"), 3, {}, rec.fn());
    CHECK(rec.take().back().kind == GenerateEventKind::kError);
    CHECK(rec.take().back().message.find("not found") != std::string::npos);

    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 4, 80.0, 1.0));
    Recorder rec2;
    int frames = 0;
    Generate(Request(dir, "moving.mp4", "standard"), 4, [&] { return ++frames > 10; }, rec2.fn());
    CHECK(rec2.take().back().kind == GenerateEventKind::kCanceled);
    CHECK(!fs::exists(dir / "standard.funscript"));
    CHECK(!fs::exists(dir / "standard.samn"));
    fs::remove_all(dir);
}

TEST_CASE("generate: the runner does one run at a time, cancels and waits on shutdown") {
    const fs::path dir = TempDir("samn_generate_runner");
    REQUIRE(clips::WriteMoving(dir / "long.mp4", 30, 80.0, 1.0));
    Recorder rec;
    {
        GenerateRunner runner(rec.fn());
        const uint64_t a = runner.Start(Request(dir, "long.mp4", "standard"));
        CHECK(a == 1);
        CHECK(runner.running());
        CHECK(runner.Start(Request(dir, "long.mp4", "weich")) == 0);  // single instance
        runner.Cancel();
        runner.Wait();
        CHECK(!runner.running());
        const auto ev = rec.take();
        REQUIRE(!ev.empty());
        CHECK(ev.back().kind == GenerateEventKind::kCanceled);
        CHECK(ev.back().run_id == 1);

        // A new run gets a new id; leaving the scope cancels and joins it.
        CHECK(runner.Start(Request(dir, "long.mp4", "standard")) == 2);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto ev = rec.take();
    CHECK(ev.back().run_id == 2);
    CHECK(ev.back().kind == GenerateEventKind::kCanceled);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
    CHECK(!fs::exists(dir / "standard.funscript"));
    fs::remove_all(dir);
}
