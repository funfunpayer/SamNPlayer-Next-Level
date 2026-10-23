#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
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
    {
        // Closed before the overwrite: Windows refuses to replace an open file.
        std::ifstream in(dir / "standard.funscript");
        std::stringstream ss;
        ss << in.rdbuf();
        CHECK(ss.str() == "keep me");
    }

    req.overwrite = true;
    Generate(req, 2, {}, rec.fn());
    const auto last = rec.take().back();
    CHECK_MESSAGE(last.kind == GenerateEventKind::kDone, last.message);
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

TEST_CASE("generate: autotune with invert runs through (the Generator page's choice)") {
    const fs::path dir = TempDir("samn_generate_autotune");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 8, 80.0, 1.0));
    Recorder rec;
    GenerateRequest req = Request(dir, "moving.mp4", "autotune");
    req.signal.invert = true;
    Generate(req, 9, {}, rec.fn());
    const auto last = rec.take().back();
    CHECK_MESSAGE(last.kind == GenerateEventKind::kDone, last.message);
    fs::remove_all(dir);
}

TEST_CASE("generate: anything thrown ends the run as an error, not the app") {
    const fs::path dir = TempDir("samn_generate_throw");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 2, 80.0, 1.0));
    Recorder rec;
    Generate(Request(dir, "moving.mp4", "standard"), 5, [] () -> bool { throw std::runtime_error("boom"); }, rec.fn());
    const auto last = rec.take().back();
    CHECK(last.kind == GenerateEventKind::kError);
    CHECK(last.message == "generation failed: boom");
    CHECK(last.run_id == 5);

    // On the runner's own thread too.
    std::atomic<int> errors{0};
    {
        GenerateRunner runner([&](const GenerateEvent& e) {
            if (e.kind == GenerateEventKind::kError) ++errors;
            if (e.kind == GenerateEventKind::kProgress) throw std::runtime_error("callback failed");
        });
        REQUIRE(runner.Start(Request(dir, "moving.mp4", "standard")) != 0);
        runner.Wait();
    }
    CHECK(errors == 1);
    fs::remove_all(dir);
}

TEST_CASE("generate: contact vibration stores the recipe, the marks and the tip's path (0.7 contact marks)") {
    const fs::path dir = TempDir("samn_generate_contact");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 3, 80.0, 1.0));
    GenerateRequest req = Request(dir, "moving.mp4", "standard");
    req.contact_vibration = true;
    req.contact_vibration_curve = "soft";
    req.tip_class = "glans";
    // Around the tip's start: the path passes through it. Fixed partner so
    // the tip CSRT is not distracted (partner path still recorded).
    const cv::Point c(req.box.x + req.box.width / 2, req.box.y + req.box.height / 2);
    req.contact_area = samn::ContactMarkBox{10, 10, 50, 50, "nipples", true};
    req.extra_areas = {samn::ContactMarkBox{0, 0, 10, 10, "mouth", true}};
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    REQUIRE(rec.take().back().kind == GenerateEventKind::kDone);

    samn::Document d = samn::LoadDocument(dir / "standard.samn");
    CHECK(d.profile == "standard");
    CHECK(d.recipe.contact_vibration);
    CHECK(d.recipe.contact_vibration_curve == "soft");
    REQUIRE(d.contact_marks);
    CHECK(d.contact_marks->tip_class == "glans");
    REQUIRE(d.contact_marks->tip);
    CHECK(d.contact_marks->tip->w == req.box.width);
    REQUIRE(d.contact_marks->primary);
    CHECK(d.contact_marks->primary->cls == "nipples");
    CHECK(d.contact_marks->primary->fixed);
    REQUIRE(d.contact_marks->extras.size() == 1);
    CHECK(d.contact_marks->extras[0].fixed);
    CHECK_FALSE(d.contact_marks->drive_stroke);
    // The path: frame pixels, about every 50 ms, inside the frame.
    REQUIRE(d.trajectory);
    CHECK(d.trajectory->width > 0);
    REQUIRE(d.trajectory->tip.size() >= 30);  // 3 s at 25 fps, every second frame
    for (size_t i = 1; i < d.trajectory->tip.size(); ++i)
        CHECK(d.trajectory->tip[i].at_ms - d.trajectory->tip[i - 1].at_ms >= 40);
    CHECK(d.trajectory->tip.front().x == doctest::Approx(c.x).epsilon(0.02));
    // G2b: fixed partner path centres stay on the gold box mid.
    REQUIRE(d.trajectory->partner.size() == d.trajectory->tip.size());
    CHECK(d.trajectory->partner.front().x == doctest::Approx(35.0).epsilon(0.05));
    CHECK(d.trajectory->partner.front().y == doctest::Approx(35.0).epsilon(0.05));
    CHECK(d.trajectory->partner.back().x == doctest::Approx(35.0).epsilon(0.05));
    // The Player turns tip path into contact vibration (static boxes).
    const samn::MapOptions o = samn::MapOptionsFromScript(d.ToFunscript());
    REQUIRE(o.spatial_contact);
    CHECK(o.contact_vibration);

    const auto fs_json = ReadJson(dir / "standard.funscript");
    CHECK(fs_json["metadata"]["profile"] == "standard");
    CHECK(fs_json["metadata"]["device_recipe"]["contact_vibration"] == true);
    CHECK(fs_json["metadata"]["contact_marks"]["primary"]["class"] == "nipples");
    CHECK(!fs_json["metadata"].contains("trajectory"));  // .samn only
    fs::remove_all(dir);
}

TEST_CASE("generate: non-fixed partner path follows the second disc (G2b)") {
    const fs::path dir = TempDir("samn_generate_partner");
    REQUIRE(clips::WriteTipAndPartner(dir / "two.mp4", 3, 60.0, 80.0));
    GenerateRequest req = Request(dir, "two.mp4", "standard");
    req.box = cv::Rect(65, 85, 70, 70);  // tip disc at (100, ~120)
    req.contact_vibration = true;
    req.contact_area = samn::ContactMarkBox{165, 145, 70, 70, "nipples", false};
    req.camera_compensation = false;
    req.scene_cuts = false;
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    REQUIRE(rec.take().back().kind == GenerateEventKind::kDone);
    const samn::Document d = samn::LoadDocument(dir / "standard.samn");
    REQUIRE(d.trajectory);
    REQUIRE(d.trajectory->partner.size() == d.trajectory->tip.size());
    REQUIRE(d.trajectory->partner.size() >= 20);
    double p_lo = d.trajectory->partner.front().x, p_hi = p_lo;
    for (const auto& p : d.trajectory->partner) {
        p_lo = std::min(p_lo, p.x);
        p_hi = std::max(p_hi, p.x);
    }
    // Partner moves horizontally ~±40 px around 200 → range well above a fixed box.
    CHECK(p_hi - p_lo >= 30.0);
    fs::remove_all(dir);
}

// Tf/Tj + gold contact: stroke from tip↔partner distance; recipe + drive_stroke.
TEST_CASE("generate: distance profile stroke follows tip-partner gap (Tf/Tj)") {
    const fs::path dir = TempDir("samn_generate_tj_gap");
    REQUIRE(clips::WriteTipAndPartner(dir / "two.mp4", 3, 8.0, 80.0));
    GenerateRequest req = Request(dir, "two.mp4", "tj");
    req.box = cv::Rect(65, 85, 70, 70);
    req.contact_area = samn::ContactMarkBox{165, 145, 70, 70, "nipples", false};
    req.camera_compensation = false;
    req.scene_cuts = false;
    req.auto_ozone = false;
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    REQUIRE(rec.take().back().kind == GenerateEventKind::kDone);
    const samn::Document d = samn::LoadDocument(dir / "tj.samn");
    CHECK(d.profile == "tj");
    REQUIRE(d.contact_marks);
    CHECK(d.contact_marks->drive_stroke);
    REQUIRE(d.trajectory);
    REQUIRE(d.trajectory->partner.size() == d.trajectory->tip.size());
    // Actions span the distance-profile clamp (20–90), not a flat tip-Y hold.
    REQUIRE(d.general.size() >= 4);
    int lo = d.general.front().pos, hi = lo;
    for (const auto& a : d.general) {
        lo = std::min(lo, a.pos);
        hi = std::max(hi, a.pos);
        CHECK(a.pos >= 20);
        CHECK(a.pos <= 90);
    }
    CHECK(hi - lo >= 15);
    fs::remove_all(dir);
}

TEST_CASE("generate: without contact vibration only the tip box is stored") {
    const fs::path dir = TempDir("samn_generate_nocontact");
    REQUIRE(clips::WriteMoving(dir / "moving.mp4", 2, 80.0, 1.0));
    GenerateRequest req = Request(dir, "moving.mp4", "standard");
    req.tip_class = "penis";
    req.contact_area = samn::ContactMarkBox{10, 10, 60, 60, "mouth", false};
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    REQUIRE(rec.take().back().kind == GenerateEventKind::kDone);
    const samn::Document d = samn::LoadDocument(dir / "standard.samn");
    CHECK_FALSE(d.recipe.contact_vibration);
    REQUIRE(d.contact_marks);
    CHECK(d.contact_marks->tip_class == "penis");
    CHECK(d.contact_marks->tip);
    CHECK_FALSE(d.contact_marks->primary);
    CHECK_FALSE(d.trajectory);
    const auto fs_json = ReadJson(dir / "standard.funscript");
    CHECK(!fs_json["metadata"].contains("device_recipe"));
    CHECK(fs_json["metadata"]["contact_marks"]["tip"]["class"] == "penis");
    fs::remove_all(dir);
}
