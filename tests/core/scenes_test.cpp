// Port of 0.7 cmd/gui-wails/app_chapter_scenes_test.go and
// generator/scene_proposals_test.go (the loader part).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "samn/scenes.h"

using namespace samn;
namespace fs = std::filesystem;

namespace {

fs::path WriteScene(const char* name, const std::string& body) {
    const fs::path dir = fs::temp_directory_path() / "samn_scenes_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    std::ofstream(p, std::ios::binary) << body;
    return p;
}

std::vector<SceneProposal> TwoScenes() {
    // blowjob in the first 9 s, titjob 11-20 s.
    return {{2000, 0, 9000, "blowjob", 0.9}, {14000, 11000, 20000, "titjob", 0.8}};
}

}  // namespace

TEST_CASE("scene proposals: the scene_roles.py file loads, sorted, bad boxes dropped") {
    const fs::path p = WriteScene("clip.scene.json", R"({"version":1,"tool":"scene_roles","width":1280,"height":720,"windows":[],
     "proposals":[
      {"t_ms":14000,"start_ms":10000,"end_ms":18000,"scene_type":"titjob","confidence":0.55,
       "primary":{"x":480,"y":320,"w":240,"h":240,"score":0.55,"class":"breasts"}},
      {"t_ms":4000,"start_ms":0,"end_ms":8000,"scene_type":"blowjob","confidence":0.8,
       "primary":{"x":560,"y":400,"w":160,"h":80,"score":0.8,"class":"mouth"}},
      {"t_ms":24000,"start_ms":20000,"end_ms":28000,"scene_type":null,"confidence":0.3,
       "primary":{"x":1200,"y":0,"w":200,"h":80,"score":0.3,"class":"hand"}},
      {"t_ms":34000,"start_ms":30000,"end_ms":38000,"scene_type":null,"confidence":0.6,
       "primary":{"x":100,"y":100,"w":80,"h":80,"score":0.6,"class":"hand_right"}}]})");
    std::string error;
    const auto s = LoadSceneProposals(p, &error);
    REQUIRE_MESSAGE(s, error);
    REQUIRE(s->size() == 3);  // the out-of-frame primary is dropped
    CHECK((*s)[0].t_ms == 4000);
    CHECK((*s)[0].scene_type == "blowjob");
    CHECK((*s)[0].confidence == doctest::Approx(0.8));
    CHECK((*s)[2].scene_type.empty());  // null = unknown
    CHECK(SceneProposalsPath(fs::path("/v/clip.mp4")) == fs::path("/v/clip.scene.json"));
}

TEST_CASE("scene proposals: wrong version, no size, not JSON and a missing file are errors") {
    for (const char* body : {R"({"version":2,"width":10,"height":10,"proposals":[]})",
                             R"({"version":1,"proposals":[]})", "nope"}) {
        std::string error;
        CHECK_FALSE(LoadSceneProposals(WriteScene("bad.scene.json", body), &error));
        CHECK(!error.empty());
    }
    CHECK_FALSE(LoadSceneProposals(fs::temp_directory_path() / "samn_scenes_test" / "missing.json"));
}

TEST_CASE("chapter scenes: scene detection suggests per chapter; nothing is applied before saving") {
    const std::vector<Chapter> chapters{{"A", 0, 0}, {"B", 10000, 0}};
    auto got = ChapterScenes(chapters, 20000, {}, TwoScenes(), std::nullopt);
    REQUIRE(got.size() == 2);
    CHECK(got[0].suggested == "oral");
    CHECK(got[0].source == "scene detection");
    CHECK(got[0].confidence > 0.5);
    CHECK(got[0].end_ms == 10000);  // open end runs to the next chapter
    CHECK(got[1].suggested == "breast");
    CHECK(got[1].end_ms == 20000);  // the last runs to the script end
    CHECK_FALSE(got[0].has_feel);
    CHECK(SceneRangesOf(got).empty());

    // Saving the suggestions gives each chapter its feel.
    for (auto& s : got) {
        s.feel = s.suggested;
        s.has_feel = true;
    }
    const auto ranges = SceneRangesOf(got);
    REQUIRE(ranges.size() == 2);
    got = ChapterScenes(chapters, 20000, ranges, TwoScenes(), std::nullopt);
    CHECK(got[0].has_feel);
    CHECK(got[0].feel == "oral");
    CHECK(got[1].feel == "breast");
}

TEST_CASE("chapter scenes: Classic is a saved feel too; a moved chapter loses its feel") {
    const std::vector<Chapter> chapters{{"A", 0, 5000}};
    auto got = ChapterScenes(chapters, 9000, {{0, 5000, ""}}, {}, std::nullopt);
    REQUIRE(got.size() == 1);
    CHECK(got[0].has_feel);
    CHECK(got[0].feel.empty());
    got = ChapterScenes({{"A", 0, 6000}}, 9000, {{0, 5000, "oral"}}, {}, std::nullopt);
    CHECK_FALSE(got[0].has_feel);
}

TEST_CASE("chapter scenes: without scene detection the contact mark suggests for every chapter") {
    ContactMarks cm;
    ContactMarkBox b;
    b.w = b.h = 5;
    b.cls = "mouth";
    cm.primary = b;
    const auto got = ChapterScenes({{"All", 0, 0}}, 1000, {}, {}, cm);
    REQUIRE(got.size() == 1);
    CHECK(got[0].suggested == "oral");
    CHECK(got[0].source == "contact mark");
    CHECK(got[0].confidence == doctest::Approx(0.5));
    CHECK(ContactClassScene("Nipples") == "breast");
    CHECK(ContactClassScene("hand_right") == "hand");
    CHECK(ContactClassScene("glans").empty());
}

TEST_CASE("chapter scenes: the saved oral feel keeps a seal at the shallow point (mapper)") {
    Document d;
    d.version = SamnCurrentVersion;
    d.kind = SamnKind;
    for (int i = 0; i <= 40; ++i) d.general.push_back({int64_t(i) * 500, i % 2 ? 90 : 10});
    d.duration_ms = 20000;
    d.recipe.sync = "independent";
    d.recipe.scene_ranges = {{0, 10000, "oral"}};
    const MapOptions o = MapOptionsFromScript(d.ToFunscript());
    const auto frames = ToIntensityCurve(d.general, o);
    REQUIRE(frames);
    double min_suc = 1;
    for (const Frame& f : *frames)
        if (f.at < 9000) min_suc = std::min(min_suc, f.suction);
    CHECK(min_suc >= 0.25);
}
