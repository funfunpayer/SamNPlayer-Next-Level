#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <iterator>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "samn/marks.h"

namespace fs = std::filesystem;
using samn::Bookmark;
using samn::Chapter;
using samn::MarksError;

namespace {

// Per-test temp directory (pytest's tmp_path), removed on destruction.
struct TmpDir {
    fs::path dir;
    TmpDir() {
        static std::atomic<unsigned> n{0};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("samn_marks_" + std::to_string(stamp) + "_" + std::to_string(n++));
        fs::create_directories(dir);
    }
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path operator/(const char* name) const { return dir / name; }
};

void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// --- ported Go tests ---------------------------------------------------------

TEST_CASE("marks: bookmarks round trip") {
    TmpDir t;
    auto path = t / "a.funscript";
    WriteText(path, R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{"creator":"t"}})");
    samn::SaveBookmarks(path, {Bookmark{"A", 100}, Bookmark{"B", 500}});
    auto out = samn::LoadBookmarks(path);
    REQUIRE(out.size() == 2);
    CHECK(out[0].time_ms == 100);
    CHECK(out[1].name == "B");
}

TEST_CASE("marks: bookmarks accept float seconds") {
    TmpDir t;
    auto path = t / "ofs.funscript";
    // OFS-style: fractional seconds in metadata
    WriteText(path, R"({"actions":[{"at":0,"pos":0},{"at":2000,"pos":100}],"metadata":{"bookmarks":[{"name":"m","time":12.5}],"chapters":[{"name":"c","startTime":1.5,"endTime":3.25}]}})");
    auto bm = samn::LoadBookmarks(path);
    REQUIRE(bm.size() == 1);
    CHECK(bm[0].time_ms == 12500);
    auto ch = samn::LoadChapters(path);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].start_ms == 1500);
    CHECK(ch[0].end_ms == 3250);
}

TEST_CASE("marks: chapters round trip") {
    TmpDir t;
    auto path = t / "a.funscript";
    WriteText(path, R"({"actions":[{"at":0,"pos":10},{"at":2000,"pos":90}]})");
    samn::SaveChapters(path, {Chapter{"Intro", 0, 500}});
    auto ch = samn::LoadChapters(path);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].name == "Intro");
}

TEST_CASE("marks: load chapters and bookmarks timestamp strings") {
    // OFS 3 writes chapter/bookmark times as timestamp text ("00:01:23.456").
    TmpDir t;
    auto p = t / "x.funscript";
    WriteText(p, R"({"actions":[{"at":0,"pos":0},{"at":200000,"pos":100}],"metadata":{
	 "chapters":[{"name":"Intro","startTime":"00:00:00.000","endTime":"00:01:23.456"},
	             {"name":"Main","startTime":"1:23.456","endTime":"01:02:03"}],
	 "bookmarks":[{"name":"b","time":"00:00:12.5"}]}})");
    auto ch = samn::LoadChapters(p);
    CHECK(ch == std::vector<Chapter>{Chapter{"Intro", 0, 83456}, Chapter{"Main", 83456, 3723000}});
    auto bm = samn::LoadBookmarks(p);
    REQUIRE(bm.size() == 1);
    CHECK(bm[0].time_ms == 12500);
    // Unreadable text stays an error.
    WriteText(p, R"({"actions":[{"at":0,"pos":0}],"metadata":{"bookmarks":[{"name":"b","time":"soon"}]}})");
    CHECK_THROWS_AS(samn::LoadBookmarks(p), MarksError);
}

// --- extra checks (no Go test) -----------------------------------------------

TEST_CASE("marks: rounding is half away from zero") {
    CHECK(samn::NormalizeTimeToMs(0.0005) == 1);
    CHECK(samn::NormalizeTimeToMs(2.5e-3) == 3);
    CHECK(samn::NormalizeTimeToMs(-0.0025) == -3);
    CHECK(samn::NormalizeTimeToMs(12.5) == 12500);
    CHECK(samn::NormalizeTimeToMs(2500.0) == 2500);  // integral value stays ms
    CHECK(samn::NormalizeTimeToMs(100000.5) == 100001);  // >= 1e5 stays ms, rounded
    CHECK(samn::NormalizeTimeToMs(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(samn::ParseClockMs("0:00.0005") == 1);
}

TEST_CASE("marks: ParseClockMs edges") {
    CHECK(samn::ParseClockMs("1:23.5") == 83500);
    CHECK(samn::ParseClockMs("01:02:03") == 3723000);
    CHECK(!samn::ParseClockMs("12").has_value());
    CHECK(!samn::ParseClockMs("1.5:30").has_value());  // fraction only in seconds
    CHECK(!samn::ParseClockMs("1:2:3:4").has_value());
    CHECK(!samn::ParseClockMs("-1:30").has_value());
    CHECK(!samn::ParseClockMs("a:b").has_value());
}

TEST_CASE("marks: FlexTimeMs values") {
    using nlohmann::json;
    CHECK(samn::FlexTimeMs(json(nullptr)) == 0);
    CHECK(samn::FlexTimeMs(json(1500)) == 1500);
    CHECK(samn::FlexTimeMs(json("1.5")) == 1500);
    CHECK(samn::FlexTimeMs(json("00:00:12.5")) == 12500);
    const json bad[] = {json(true),         json::array(),
                        json::object(),     json("soon"),
                        json("1e999"),      json(std::numeric_limits<double>::infinity())};
    for (const auto& b : bad) CHECK_THROWS_AS(samn::FlexTimeMs(b), MarksError);
}

TEST_CASE("marks: save sorts and preserves other keys") {
    TmpDir t;
    auto p = t / "s.funscript";
    WriteText(p, "{ \"version\": \"1.0\", \"actions\": [ {\"at\": 0, \"pos\": 1.0} ],\n"
                 " \"metadata\": {\"creator\": \"t\", \"chapters\": [{\"name\":\"x\",\"startTime\":5}], \"z\": 1.0} }");
    samn::SaveBookmarks(p, {Bookmark{"late", 900}, Bookmark{"early", 100}});
    std::string text = ReadText(p);
    auto doc = nlohmann::json::parse(text);
    auto& bms = doc["metadata"]["bookmarks"];
    REQUIRE(bms.size() == 2);
    CHECK(bms[0]["name"] == "early");
    CHECK(bms[1]["name"] == "late");
    CHECK(doc["metadata"]["creator"] == "t");
    CHECK(doc["metadata"]["chapters"] == nlohmann::json::parse(R"([{"name":"x","startTime":5}])"));
    CHECK(doc["version"] == "1.0");
    CHECK(text.find("\"pos\":1.0") != std::string::npos);  // untouched values keep their text
    CHECK(text.find("\"z\":1.0") != std::string::npos);
    CHECK(text.find("\"actions\"") < text.find("\"metadata\""));
    CHECK(text.find("\"metadata\"") < text.find("\"version\""));
}

TEST_CASE("marks: save chapters omits zero end and sorts") {
    TmpDir t;
    auto p = t / "c.funscript";
    WriteText(p, R"({"actions":[]})");
    samn::SaveChapters(p, {Chapter{"b", 2000}, Chapter{"a", 0, 1000}});
    auto doc = nlohmann::json::parse(ReadText(p));
    CHECK(doc["metadata"]["chapters"] ==
          nlohmann::json::parse(R"([{"name":"a","startTime":0,"endTime":1000},{"name":"b","startTime":2000}])"));
}

TEST_CASE("marks: save is atomic and leaves no temp") {
    TmpDir t;
    auto p = t / "t.funscript";
    WriteText(p, R"({"actions":[]})");
    samn::SaveBookmarks(p, {Bookmark{"a", 1}});
    std::set<std::string> names;
    for (const auto& e : fs::directory_iterator(t.dir)) names.insert(e.path().filename().string());
    CHECK(names == std::set<std::string>{"t.funscript"});
}

TEST_CASE("marks: errors") {
    TmpDir t;
    CHECK_THROWS_AS(samn::LoadBookmarks(t / "missing.funscript"), MarksError);
    auto p = t / "bad.funscript";
    WriteText(p, "not json");
    CHECK_THROWS_AS(samn::LoadBookmarks(p), MarksError);
    CHECK_THROWS_AS(samn::SaveBookmarks(p, {}), MarksError);
    WriteText(p, R"({"metadata":{"bookmarks":{"a":1}}})");
    CHECK_THROWS_AS(samn::LoadBookmarks(p), MarksError);
    WriteText(p, R"({"metadata":null})");
    CHECK(samn::LoadBookmarks(p).empty());
    CHECK(samn::LoadChapters(p).empty());
}

TEST_CASE("range marker: 0.7 sidecar file") {
    const auto dir = std::filesystem::temp_directory_path() / ("samn_marker_" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    const auto script = dir / "clip.funscript";
    CHECK_FALSE(samn::LoadMarker(script).has_value());
    samn::SaveMarker(script, {1500, 4200});
    {
        // Closed before the file is removed below (Windows locks open files).
        std::ifstream in(dir / "clip.funscript.marker.json", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text == R"({"startMs":1500,"endMs":4200})");  // same bytes as Go
    }
    CHECK(samn::LoadMarker(script) == samn::RangeMarker{1500, 4200});
    // An empty range removes it.
    samn::SaveMarker(script, {10, 10});
    CHECK_FALSE(std::filesystem::exists(dir / "clip.funscript.marker.json"));
    // A broken file is ignored.
    std::ofstream(dir / "clip.funscript.marker.json") << "{broken";
    CHECK_FALSE(samn::LoadMarker(script).has_value());
    std::filesystem::remove_all(dir);
}

// 0.7 playback.js chapterRanges
TEST_CASE("ChapterRanges: sorted, open ends run on, names filled") {
    std::vector<samn::Chapter> in{{"B", 5000, 0}, {"", 1000, 3000}, {"C", 9000, 8000}};
    const auto r = samn::ChapterRanges(in, 12000);
    REQUIRE(r.size() == 3);
    CHECK(r[0].name == "Chapter 1");
    CHECK(r[0].start_ms == 1000);
    CHECK(r[0].end_ms == 3000);
    CHECK(r[0].index == 0);
    CHECK(r[1].name == "B");
    CHECK(r[1].end_ms == 9000);   // open end -> next start
    CHECK(r[2].end_ms == 12000);  // end before start -> script end
    CHECK(r[2].index == 2);
    CHECK(samn::ChapterRanges({}, 1000).empty());
    // Script end before the start: never negative.
    const auto z = samn::ChapterRanges({{"X", 5000, 0}}, 2000);
    CHECK(z[0].end_ms == 5000);
}

// 0.7 playback.js addSceneMarkerAt
TEST_CASE("AddSceneMarker cuts the spanning chapter and runs to the next") {
    const std::vector<samn::Chapter> in{{"A", 0, 10000}, {"B", 20000, 0}};
    const auto r = samn::AddSceneMarker(in, 4000);
    REQUIRE(r);
    REQUIRE(r->size() == 3);
    CHECK((*r)[0].end_ms == 4000);  // A cut at the marker
    CHECK((*r)[1].name == "Scene 3");
    CHECK((*r)[1].start_ms == 4000);
    CHECK((*r)[1].end_ms == 20000);  // to the next start
    CHECK((*r)[2].end_ms == 0);      // B untouched
    const auto last = samn::AddSceneMarker(in, 25000);  // inside open-ended B
    REQUIRE(last);
    CHECK((*last)[1].end_ms == 25000);
    CHECK((*last)[2].end_ms == 0);  // nothing after: open end
    CHECK_FALSE(samn::AddSceneMarker(in, 20100));  // within 250 ms of B
    CHECK(samn::AddSceneMarker({}, -5)->front().start_ms == 0);
}

TEST_CASE("PatchActions replaces the actions and keeps everything else") {
    const auto path = std::filesystem::temp_directory_path() / "samn_patch_actions.funscript";
    {
        std::ofstream(path) << R"({"version":"1.0","inverted":false,"range":90,)"
                               R"("actions":[{"at":0,"pos":0},{"at":500,"pos":100}],)"
                               R"("metadata":{"creator":"SamNPlayer","oMarkers":[{"startMs":1,"endMs":2,"kind":"o","intensity":1}]},)"
                               R"("x_unknown":{"keep":[1,2]}})";
    }
    samn::PatchActions(path, {{900, 30}, {0, 10}, {400, 70}});
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    const auto j = nlohmann::json::parse(ss.str());
    REQUIRE(j["actions"].size() == 3);
    CHECK(j["actions"][0]["at"] == 0);   // written in time order
    CHECK(j["actions"][2]["at"] == 900);
    CHECK(j["actions"][2]["pos"] == 30);
    CHECK(j["metadata"]["creator"] == "SamNPlayer");
    CHECK(j["metadata"]["oMarkers"].size() == 1);
    CHECK(j["x_unknown"]["keep"][1] == 2);
    CHECK(j["range"] == 90);
    CHECK(samn::Parse(ss.str()).actions.size() == 3);
    std::filesystem::remove(path);
    CHECK_THROWS_AS(samn::PatchActions(path, {{0, 0}}), samn::MarksError);
}
