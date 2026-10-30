#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "samn/project.h"

namespace fs = std::filesystem;
using samn::Bookmark;
using samn::Chapter;
using samn::LoadProject;
using samn::Project;
using samn::ProjectError;
using samn::ProjectPathFor;
using samn::RangeMarker;
using samn::SaveProject;

namespace {

struct TmpDir {
    fs::path dir;
    TmpDir() {
        static std::atomic<unsigned> n{0};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("samn_project_" + std::to_string(stamp) + "_" + std::to_string(n++));
        fs::create_directories(dir);
    }
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void WriteText(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

}  // namespace

// Go: TestProjectSidecar
TEST_CASE("TestProjectSidecar") {
    TmpDir tmp;
    const fs::path media = tmp.dir / "clip.mp4";
    const fs::path path = ProjectPathFor(media);
    CHECK(path.filename() == "clip.snp.json");
    Project p;
    p.video_path = "clip.mp4";
    p.script_path = "clip.funscript";
    p.offset_ms = 40;
    SaveProject(path, p);
    Project got = LoadProject(path);
    CHECK(got.offset_ms == 40);
    CHECK(got.version == 1);
    CHECK(got.video_path == "clip.mp4");
    CHECK(got.script_path == "clip.funscript");
    CHECK_FALSE(got.loop_marker.has_value());
}

TEST_CASE("ProjectPathFor follows Go filepath.Ext") {
    CHECK(ProjectPathFor("a/b/clip.mp4") == fs::path("a/b/clip.snp.json"));
    CHECK(ProjectPathFor("clip") == fs::path("clip.snp.json"));
    CHECK(ProjectPathFor("a.b/clip") == fs::path("a.b/clip.snp.json"));
    CHECK(ProjectPathFor("x.tar.gz") == fs::path("x.tar.snp.json"));
    CHECK(ProjectPathFor(".hidden") == fs::path(".snp.json"));
}

// A media path outside the Windows ANSI code page round-trips.
TEST_CASE("ProjectPathFor keeps a non-ASCII path") {
    TmpDir tmp;
    const fs::path folder = tmp.dir / fs::u8path(u8"テスト");
    fs::create_directories(folder);
    const fs::path path = ProjectPathFor(folder / fs::u8path(u8"clip ✓.samn"));
    CHECK(path == folder / fs::u8path(u8"clip ✓.snp.json"));
    CHECK(path.filename().u8string() == u8"clip ✓.snp.json");
    Project p;
    p.video_path = u8"clip ✓.mp4";
    p.offset_ms = 12;
    SaveProject(path, p);
    CHECK(LoadProject(path).offset_ms == 12);
    CHECK(LoadProject(path).video_path == u8"clip ✓.mp4");
}

// Expected bytes come from Go: json.MarshalIndent(p, "", "  ") of the same Project
// (SaveProject on /home/user/SamNPlayer funscript, go1.24).
TEST_CASE("SaveProject writes the bytes of Go MarshalIndent") {
    TmpDir tmp;
    {
        Project p;
        p.video_path = "clip.mp4";
        p.script_path = "clip.funscript";
        p.offset_ms = 40;
        SaveProject(tmp.dir / "a", p);
        CHECK(ReadText(tmp.dir / "a") ==
              "{\n  \"version\": 1,\n  \"videoPath\": \"clip.mp4\",\n  \"scriptPath\": \"clip.funscript\",\n"
              "  \"offsetMs\": 40\n}");
    }
    {
        Project p;
        p.version = 3;
        p.video_path = "C:\\v <a>&\"\xC3\xA9\xE2\x80\xA8.mp4";
        p.script_path = "s\n\t.funscript";
        p.offset_ms = -250;
        p.seek_ms = 12345;
        p.loop_marker = RangeMarker{1000, 5000};
        p.bookmarks = {Bookmark{"A<b>", 100}, Bookmark{"", 0}};
        p.chapters = {Chapter{"Intro", 0, 500}, Chapter{"Open", 700, 0}};
        p.last_opened_ms = 1760600000000;
        SaveProject(tmp.dir / "b", p);
        CHECK(ReadText(tmp.dir / "b") ==
              "{\n  \"version\": 3,\n  \"videoPath\": \"C:\\\\v \\u003ca\\u003e\\u0026\\\"\xC3\xA9\\u2028.mp4\",\n"
              "  \"scriptPath\": \"s\\n\\t.funscript\",\n  \"offsetMs\": -250,\n  \"seekMs\": 12345,\n"
              "  \"loopMarker\": {\n    \"startMs\": 1000,\n    \"endMs\": 5000\n  },\n"
              "  \"bookmarks\": [\n    {\n      \"name\": \"A\\u003cb\\u003e\",\n      \"time\": 100\n    },\n"
              "    {\n      \"name\": \"\",\n      \"time\": 0\n    }\n  ],\n"
              "  \"chapters\": [\n    {\n      \"name\": \"Intro\",\n      \"startTime\": 0,\n"
              "      \"endTime\": 500\n    },\n    {\n      \"name\": \"Open\",\n      \"startTime\": 700\n    }\n  ],\n"
              "  \"lastOpenedMs\": 1760600000000\n}");

        // And back.
        Project q = LoadProject(tmp.dir / "b");
        CHECK(q.version == 3);
        CHECK(q.video_path == p.video_path);
        CHECK(q.script_path == p.script_path);
        CHECK(q.offset_ms == -250);
        CHECK(q.seek_ms == 12345);
        REQUIRE(q.loop_marker.has_value());
        CHECK(*q.loop_marker == RangeMarker{1000, 5000});
        CHECK(q.bookmarks == p.bookmarks);
        CHECK(q.chapters == p.chapters);
        CHECK(q.last_opened_ms == 1760600000000);
    }
    {
        // A set loop marker is kept even when empty (Go: non-nil pointer).
        Project p;
        p.loop_marker = RangeMarker{};
        SaveProject(tmp.dir / "c", p);
        CHECK(ReadText(tmp.dir / "c") ==
              "{\n  \"version\": 1,\n  \"loopMarker\": {\n    \"startMs\": 0,\n    \"endMs\": 0\n  }\n}");
    }
}

TEST_CASE("LoadProject: Go decoding rules and errors") {
    TmpDir tmp;
    const fs::path f = tmp.dir / "p.snp.json";
    WriteText(f, "{\"VideoPath\":\"v.mp4\",\"offsetms\":7,\"bookmarks\":null,\"extra\":[1,2]}");
    Project p = LoadProject(f);
    CHECK(p.version == 1);
    CHECK(p.video_path == "v.mp4");
    CHECK(p.offset_ms == 7);
    CHECK(p.bookmarks.empty());

    WriteText(f, "{not json");
    CHECK_THROWS_AS(LoadProject(f), ProjectError);
    WriteText(f, "{\"offsetMs\":1.5}");
    CHECK_THROWS_AS(LoadProject(f), ProjectError);
    WriteText(f, "{\"videoPath\":3}");
    CHECK_THROWS_AS(LoadProject(f), ProjectError);
    try {
        WriteText(f, "[]");
        LoadProject(f);
        FAIL("should throw");
    } catch (const ProjectError& e) {
        CHECK(std::string(e.what()).rfind("project: invalid JSON: ", 0) == 0);
    }
    WriteText(f, "null");
    CHECK(LoadProject(f).version == 1);
    CHECK_THROWS_AS(LoadProject(tmp.dir / "missing.snp.json"), ProjectError);
}
