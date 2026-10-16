#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

#include "samn/playback.h"

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

TEST_CASE("position_at") {
    std::vector<Action> a = {{0, 0}, {1000, 100}, {2000, 0}};
    CHECK(position_at(a, -5) == 0);
    CHECK(position_at(a, 500) == 50);
    CHECK(position_at(a, 1500) == 50);
    CHECK(position_at(a, 9999) == 0);
    CHECK_FALSE(position_at({}, 10).has_value());
}

TEST_CASE("loop") {
    LoopRange loop{1000, 3000};
    CHECK_FALSE(loop.jump(2999).has_value());
    CHECK(loop.jump(3000) == 1000);
    CHECK_FALSE(LoopRange{3000, 1000}.jump(5000).has_value());
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

TEST_CASE("offset saved per script") {
    TempDir t;
    fs::path p = t.path / "settings.json";
    Settings st(p);
    Session s(st);
    s.open_script("/a.funscript", {{0, 0}}, std::string(""));
    CHECK(s.set_offset(25000) == 10000);
    // Reopened later (new settings object, same file): the offset is back.
    Settings st2(p);
    Session s2(st2);
    s2.open_script("/a.funscript", {{0, 0}}, std::string(""));
    CHECK(s2.offset_ms == 10000);
    s2.open_script("/b.funscript", {{0, 0}}, std::string(""));
    CHECK(s2.offset_ms == 0);
}
