#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

#include "samn/curve_geometry.h"
#include "samn/funscript.h"
#include "samn/marks.h"
#include "samn/playback.h"
#include "samn/samn_file.h"
#include "samn/settings.h"

namespace fs = std::filesystem;
using namespace samn;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        std::random_device rd;
        path = fs::temp_directory_path() /
               ("samn_pb_" + std::to_string(rd()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace

TEST_CASE("script_time and clamp") {
    CHECK(script_time(5000, 300) == 4700);
    CHECK(script_time(100, 300) == 0);  // never below 0, as in Go
    CHECK(script_time(1000, -500) == 1500);
    CHECK(clamp_offset(20000) == 10000);
    CHECK(clamp_offset(-99999) == -10000);
}

TEST_CASE("position on the curve") {
    std::vector<Action> a = {{0, 0}, {1000, 100}, {2000, 0}};
    CHECK(PositionAt(a, -5) == 0);
    CHECK(PositionAt(a, 500) == 50);
    CHECK(PositionAt(a, 1500) == 50);
    CHECK(PositionAt(a, 9999) == 0);
    CHECK_FALSE(PositionAt({}, 10).has_value());
}

TEST_CASE("loop range") {
    // The Player loops over the marked range; an inverted one is no loop.
    CHECK(RangeMarker{1000, 3000}.Valid());
    CHECK_FALSE(RangeMarker{3000, 1000}.Valid());
}

TEST_CASE("matching video") {
    TempDir t;
    fs::path s = t.path / "Clip.funscript";
    std::ofstream(s) << "{}";
    CHECK_FALSE(find_matching_video(s.string()).has_value());
    fs::path v = t.path / "Clip.mkv";
    std::ofstream(v, std::ios::binary).close();
    auto got = find_matching_video(s.string());
    REQUIRE(got.has_value());
    CHECK(*got == v.string());
}

TEST_CASE("companion axis scripts beside a script, in 0.7's order") {
    TempDir t;
    fs::path s = t.path / "Clip.funscript";
    std::ofstream(s) << "{}";
    CHECK(companion_axis_files(s.string()).empty());
    std::ofstream(t.path / "Clip.roll.funscript") << "{}";
    std::ofstream(t.path / "Clip.surge.funscript") << "{}";
    std::ofstream(t.path / "Other.sway.funscript") << "{}";  // another script's
    std::ofstream(t.path / "Clip.lube.funscript") << "{}";   // not a companion axis
    const auto got = companion_axis_files(s.string());
    REQUIRE(got.size() == 2);
    CHECK(got[0].first == "surge");
    CHECK(got[0].second == (t.path / "Clip.surge.funscript").string());
    CHECK(got[1].first == "roll");
    // A .samn finds the same files.
    CHECK(companion_axis_files((t.path / "Clip.samn").string()).size() == 2);
}

TEST_CASE("create_companion_axis centres at 50 and never overwrites") {
    TempDir t;
    fs::path s = t.path / "Clip.funscript";
    std::ofstream(s) << R"({"actions":[{"at":0,"pos":0},{"at":2000,"pos":100}]})";
    CHECK(is_companion_axis("surge"));
    CHECK_FALSE(is_companion_axis("lube"));
    CHECK(companion_axis_path(s.string(), "surge") == (t.path / "Clip.surge.funscript").string());

    std::string err;
    auto created = create_companion_axis(s.string(), "surge", 2000, &err);
    REQUIRE(created.has_value());
    CHECK(*created == (t.path / "Clip.surge.funscript").string());
    const auto got = LoadFunscript(fs::u8path(*created)).actions;
    REQUIRE(got.size() == 2);
    CHECK(got[0].at == 0);
    CHECK(got[0].pos == 50);
    CHECK(got[1].at == 2000);
    CHECK(got[1].pos == 50);

    err.clear();
    CHECK_FALSE(create_companion_axis(s.string(), "surge", 2000, &err).has_value());
    CHECK(err.find("already exists") != std::string::npos);
    CHECK_FALSE(create_companion_axis(s.string(), "lube", 2000, &err).has_value());
}

TEST_CASE("offset saved per script") {
    TempDir t;
    fs::path p = t.path / "settings.json";
    auto saved = [](Settings& st, const std::string& script) {
        const nlohmann::json v = st.get(offset_key(script), 0);
        return clamp_offset(v.is_number() ? v.get<int64_t>() : 0);
    };
    Settings st(p);
    st.set(offset_key("/a.funscript"), clamp_offset(25000));
    CHECK(saved(st, "/a.funscript") == 10000);
    // Reopened later (new settings object, same file): the offset is back.
    Settings st2(p);
    CHECK(saved(st2, "/a.funscript") == 10000);
    CHECK(saved(st2, "/b.funscript") == 0);
}
