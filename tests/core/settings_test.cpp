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
        int asides = 0;
        std::string first;
        for (const auto& e : fs::directory_iterator(t.path)) {
            const std::string name = e.path().filename().string();
            if (name.rfind("settings.broken.", 0) == 0 && name.size() > std::string("settings.broken.").size()) {
                ++asides;
                first = name;
            }
        }
        CHECK(asides == 1);
        CHECK(first.find("settings.broken.") == 0);
        CHECK(first.find(".json") != std::string::npos);
        CHECK_FALSE(fs::exists(t.path / "settings.broken.json"));  // fixed name no longer used
        st.set("x", 1);
    }
    CHECK(Settings(p).get("x") == 1);
}

TEST_CASE("a second broken settings file keeps the first aside") {
    TempDir t;
    fs::path p = t.path / "settings.json";
    std::ofstream(p) << "{first";
    { Settings st(p); }
    // Plant a second corrupt file while the first aside stays.
    std::ofstream(p) << "{second";
    // Ensure a unique stamp collision path: copy the existing aside name's
    // stamp by writing another corrupt load in the same second if needed.
    std::string first_aside;
    for (const auto& e : fs::directory_iterator(t.path)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("settings.broken.", 0) == 0) first_aside = name;
    }
    REQUIRE_FALSE(first_aside.empty());
    { Settings st(p); }
    int asides = 0;
    bool saw_first = false;
    for (const auto& e : fs::directory_iterator(t.path)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("settings.broken.", 0) == 0) {
            ++asides;
            if (name == first_aside) saw_first = true;
        }
    }
    CHECK(asides == 2);
    CHECK(saw_first);
    // Both asides still hold their corrupt text.
    std::ifstream a(t.path / first_aside);
    std::string body((std::istreambuf_iterator<char>(a)), {});
    CHECK(body.find("first") != std::string::npos);
}

// 0.7 cmd/gui-wails/settings_store_test.go TestSettingsStoreNullFile: a file
// holding just `null` loads as empty, and a later set works and persists.
TEST_CASE("settings: a file holding null loads as empty and accepts a set") {
    TempDir t;
    fs::path p = t.path / "settings.json";
    std::ofstream(p) << "null";
    {
        Settings st(p);
        CHECK(st.get("x").is_null());
        CHECK(st.get("x", 7) == 7);
        CHECK_FALSE(fs::exists(t.path / "settings.broken.json"));
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

TEST_CASE("settings: two objects on one file never lose each other's keys") {
    const auto dir = fs::temp_directory_path() / ("samn_set2_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const auto file = dir / "settings.json";
    samn::Settings player(file), library(file);
    player.set("playback.offset.a", 100);
    library.set("library.recent", nlohmann::json::array({"x"}));
    player.set("playback.offset.b", 200);  // must not drop library.recent
    samn::Settings fresh(file);
    CHECK(fresh.get("playback.offset.a") == 100);
    CHECK(fresh.get("playback.offset.b") == 200);
    CHECK(fresh.get("library.recent") == nlohmann::json::array({"x"}));
    // A read sees the other object's newer value.
    CHECK(library.get("playback.offset.b") == 200);
    fs::remove_all(dir);
}
