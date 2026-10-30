#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "samn/samn_file.h"

// Ported from 0.7 funscript/omarker_test.go, plus a byte comparison with
// the Go output.

namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path = fs::temp_directory_path() / ("samn_om_" + std::to_string(std::random_device{}()));
    Dir() { fs::create_directories(path); }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

fs::path Write(const Dir& d, const std::string& content) {
    const auto p = d.path / "test.funscript";
    std::ofstream(p, std::ios::binary) << content;
    return p;
}

std::string Read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

samn::OMarker M(int64_t s, int64_t e, const char* kind, double i) {
    samn::OMarker m;
    m.start_ms = s;
    m.end_ms = e;
    m.kind = kind;
    m.intensity = i;
    return m;
}

}  // namespace

TEST_CASE("o-markers: empty when absent") {
    Dir d;
    const auto p = Write(d, R"({"actions":[{"at":0,"pos":0}],"metadata":{"creator":"x"}})");
    CHECK(samn::LoadOMarkers(p).empty());
}

TEST_CASE("o-markers: save and load, sorted by start") {
    Dir d;
    const auto p = Write(d, R"({"actions":[{"at":0,"pos":0}],"metadata":{"creator":"x"}})");
    samn::SaveOMarkers(p, {M(20000, 22000, "primary", 1.0), M(5000, 8000, "secondary", 0.4)});
    const auto got = samn::LoadOMarkers(p);
    REQUIRE(got.size() == 2);
    CHECK(got[0].kind == "secondary");
    CHECK(got[0].start_ms == 5000);
    CHECK(got[0].intensity == doctest::Approx(0.4));
    CHECK(got[1].kind == "primary");
    CHECK(got[1].start_ms == 20000);
}

TEST_CASE("o-markers: same bytes as 0.7, unknown fields kept, empty list writes null") {
    Dir d;
    const auto p = Write(d,
        R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}],"metadata":{"creator":"x <y> & z","quality_score":0.9,"empty":{},"list":[],"nested":{"a":[1,2,{"b":null}]}},"top":true,"inverted":false})");
    samn::SaveOMarkers(p, {M(20000, 22000, "primary", 1), M(5000, 8000, "secondary", 0.4),
                           M(9000, 9500, "secondary", 0.123456789)});
    // Reference: 0.7 funscript.SaveOMarkers on the same input (Go 1.25).
    const std::string head =
        "{\n  \"actions\": [\n    {\n      \"at\": 0,\n      \"pos\": 0\n    },\n    {\n      \"at\": 1000,\n"
        "      \"pos\": 100\n    }\n  ],\n  \"inverted\": false,\n  \"metadata\": {\n    \"creator\": \"x "
        "\\u003cy\\u003e \\u0026 z\",\n    \"empty\": {},\n    \"list\": [],\n    \"nested\": {\n      \"a\": [\n"
        "        1,\n        2,\n        {\n          \"b\": null\n        }\n      ]\n    },\n    \"oMarkers\": ";
    const std::string tail = ",\n    \"quality_score\": 0.9\n  },\n  \"top\": true\n}";
    const std::string markers =
        "[\n      {\n        \"startMs\": 5000,\n        \"endMs\": 8000,\n        \"kind\": \"secondary\",\n"
        "        \"intensity\": 0.4\n      },\n      {\n        \"startMs\": 9000,\n        \"endMs\": 9500,\n"
        "        \"kind\": \"secondary\",\n        \"intensity\": 0.123456789\n      },\n      {\n"
        "        \"startMs\": 20000,\n        \"endMs\": 22000,\n        \"kind\": \"primary\",\n"
        "        \"intensity\": 1\n      }\n    ]";
    CHECK(Read(p) == head + markers + tail);
    samn::SaveOMarkers(p, {});
    CHECK(Read(p) == head + "null" + tail);
    CHECK(samn::LoadOMarkers(p).empty());
}

TEST_CASE("o-markers: invalid markers are rejected and the file stays untouched") {
    Dir d;
    const auto p = Write(d, R"({"actions":[{"at":0,"pos":0}],"metadata":{}})");
    const std::string before = Read(p);
    CHECK_THROWS(samn::SaveOMarkers(p, {M(2000, 1000, "primary", 0.5)}));
    CHECK_THROWS(samn::SaveOMarkers(p, {M(1000, 2000, "tertiary", 0.5)}));
    CHECK_THROWS(samn::SaveOMarkers(p, {M(1000, 2000, "primary", 1.5)}));
    CHECK_THROWS(samn::SaveOMarkers(p, {M(1000, 2000, "primary", -0.1)}));
    CHECK(Read(p) == before);
}

TEST_CASE("script marks: .funscript metadata and .samn document") {
    Dir d;
    const auto fun = Write(d, R"({"actions":[{"at":0,"pos":0},{"at":1000,"pos":100}]})");
    samn::SaveScriptBookmarks(fun, {{"b", 500}});
    samn::SaveScriptChapters(fun, {{"Intro", 0, 800}});
    samn::SaveScriptOMarkers(fun, {M(100, 900, "primary", 1)});
    auto m = samn::LoadScriptMarks(fun);
    REQUIRE(m.bookmarks.size() == 1);
    CHECK(m.bookmarks[0].time_ms == 500);
    REQUIRE(m.chapters.size() == 1);
    CHECK(m.chapters[0].name == "Intro");
    REQUIRE(m.o_markers.size() == 1);

    // The same through a .samn converted from it.
    auto doc = samn::LoadFunscriptFile(fun);
    const auto sp = d.path / "test.samn";
    samn::SaveDocument(sp, doc);
    samn::SaveScriptBookmarks(sp, {{"x", 700}, {"y", 100}});
    samn::SaveScriptOMarkers(sp, {});
    m = samn::LoadScriptMarks(sp);
    REQUIRE(m.bookmarks.size() == 2);
    CHECK(m.o_markers.empty());
    REQUIRE(m.chapters.size() == 1);  // kept
    CHECK_THROWS(samn::SaveScriptOMarkers(sp, {M(5, 1, "primary", 1)}));
}

// Paths with characters outside the Windows ANSI code page: the marks still
// find the .samn by its extension (path::string() would throw there).
TEST_CASE("script marks: a .samn under a non-ASCII path") {
    Dir d;
    const fs::path folder = d.path / fs::u8path(u8"テスト");
    fs::create_directories(folder);
    const fs::path sp = folder / fs::u8path(u8"clip ✓.samn");
    samn::Document doc;
    doc.version = samn::SamnCurrentVersion;
    doc.kind = samn::SamnKind;
    doc.general = {{0, 0}, {1000, 100}};
    samn::SaveDocument(sp, doc);
    samn::SaveScriptBookmarks(sp, {{"b", 500}});
    samn::SaveScriptChapters(sp, {{"Intro", 0, 800}});
    samn::SaveScriptOMarkers(sp, {M(100, 900, "primary", 1)});
    const auto m = samn::LoadScriptMarks(sp);
    REQUIRE(m.bookmarks.size() == 1);
    CHECK(m.bookmarks[0].time_ms == 500);
    REQUIRE(m.chapters.size() == 1);
    REQUIRE(m.o_markers.size() == 1);
    // Written into the document, not as a funscript's metadata.
    CHECK(samn::LoadDocument(sp).bookmarks.size() == 1);
    // The Player finds the .samn beside its .funscript (UTF-8 paths).
    const std::string funscript = (folder / fs::u8path(u8"clip ✓.funscript")).u8string();
    CHECK(samn::PreferSamnCompanion(funscript) == sp.u8string());
}
