#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

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
               ("samn_set_" + std::to_string(rd()) + "_" +
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

TEST_CASE("broken settings file is kept aside") {
    TempDir t;
    fs::path p = t.path / "settings.json";
    std::ofstream(p) << "{oops";
    {
        Settings st(p);
        CHECK(st.get("x").is_null());
        CHECK(fs::exists(t.path / "settings.broken.json"));
        st.set("x", 1);
    }
    CHECK(Settings(p).get("x") == 1);
}

TEST_CASE("settings: set_many, defaults, atomic write leaves no temp files") {
    TempDir t;
    fs::path p = t.path / "sub" / "settings.json";
    Settings st(p);
    CHECK(st.get("a", 5) == 5);
    st.set_many({{"a", 1}, {"b", "text"}});
    Settings again(p);
    CHECK(again.get("a") == 1);
    CHECK(again.get("b") == "text");
    int files = 0;
    for (auto& e : fs::directory_iterator(p.parent_path())) {
        (void)e;
        ++files;
    }
    CHECK(files == 1);
}

#ifndef _WIN32
TEST_CASE("config_dir: SAMN_CONFIG_DIR wins") {
    setenv("SAMN_CONFIG_DIR", "/tmp/samn_cfg_test", 1);
    CHECK(config_dir() == fs::path("/tmp/samn_cfg_test"));
    unsetenv("SAMN_CONFIG_DIR");
    setenv("XDG_CONFIG_HOME", "/tmp/xdg_test", 1);
    CHECK(config_dir() == fs::path("/tmp/xdg_test/SamNPlayer2"));
    unsetenv("XDG_CONFIG_HOME");
}
#endif
