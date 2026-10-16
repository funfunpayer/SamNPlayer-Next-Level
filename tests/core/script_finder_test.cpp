#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>

#include "samn/script_finder.h"

namespace fs = std::filesystem;
using namespace samn;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> counter{0};
        std::random_device rd;
        path = fs::temp_directory_path() /
               ("samn_sf_" + std::to_string(rd()) + "_" +
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

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "{}";
}

std::string name_of(const std::string& p) { return fs::path(p).filename().string(); }

}  // namespace

TEST_CASE("find_script_for_video") {
    TempDir t;
    fs::path video = t.path / "Clip One.mp4";
    touch(video);

    CHECK_FALSE(find_script_for_video(video.string()).found);

    // Axis companions do not count as the script.
    touch(t.path / "Clip One.surge.funscript");
    CHECK_FALSE(find_script_for_video(video.string()).found);

    touch(t.path / "clip one.funscript");
    auto m = find_script_for_video(video.string());
    CHECK(m.found);
    CHECK(name_of(m.script_path) == "clip one.funscript");

    touch(t.path / "Clip One.funscript");
    std::string found = name_of(find_script_for_video(video.string()).script_path);
    if (fs::exists(t.path / "CLIP ONE.FUNSCRIPT")) {
        // Case-insensitive file system: same file, keeps its first name.
        CHECK(found == "clip one.funscript");
    } else {
        CHECK(found == "Clip One.funscript");
    }

    touch(t.path / "Clip One.samn");
    CHECK(name_of(find_script_for_video(video.string()).script_path) == "Clip One.samn");

    CHECK_FALSE(find_script_for_video("").found);
}

TEST_CASE("find_script_for_video: folders, names, variants") {
    TempDir t;
    fs::path videos = t.path / "videos";
    fs::path scripts = t.path / "scripts";
    fs::create_directories(videos);
    fs::create_directories(scripts);
    fs::path video = videos / "Some_Studio - Clip.Title (2019) [1080p].mp4";
    touch(video);

    // Only in the script folder, written differently, one level down.
    touch(scripts / "authorA" / "some studio clip title.funscript");
    auto m = find_script_for_video(video.string(), {scripts.string()});
    CHECK(m.found);
    CHECK(name_of(m.script_path) == "some studio clip title.funscript");
    CHECK_FALSE(m.variants[0].beside);

    // Variants: Soft, another author; not a sequel, not an axis file.
    touch(scripts / "Some Studio - Clip Title (Soft).funscript");
    touch(scripts / "authorB" / "Some Studio - Clip Title - by Bee.funscript");
    touch(scripts / "Some Studio - Clip Title 2.funscript");
    touch(scripts / "Some Studio - Clip Title.twist.funscript");
    m = find_script_for_video(video.string(), {scripts.string()});
    std::set<std::string> labels;
    for (const auto& v : m.variants) labels.insert(v.label);
    CHECK(m.variants.size() == 3);
    CHECK(labels.count(""));
    CHECK(labels.count("soft"));
    CHECK(labels.count("by bee"));
    // the script with the video's own name comes first
    CHECK(m.variants[0].label == "");

    // A script beside the video beats the folder.
    touch(videos / "Some_Studio - Clip.Title (2019) [1080p].funscript");
    m = find_script_for_video(video.string(), {scripts.string()});
    CHECK(m.variants[0].beside);
    CHECK(fs::path(m.script_path).parent_path() == videos);

    // A .samn and its .funscript count once.
    touch(videos / "Some_Studio - Clip.Title (2019) [1080p].samn");
    m = find_script_for_video(video.string(), {scripts.string()});
    CHECK(fs::path(m.script_path).extension() == ".samn");
    CHECK(m.variants.size() == 4);
}

TEST_CASE("variant_label") {
    struct Row {
        const char *script, *video, *label;
        bool ok;
    };
    const Row rows[] = {
        {"Clip", "Clip", "", true},
        {"clip [4K]", "Clip (2020)", "", true},
        {"Clip (Hard)", "Clip", "hard", true},
        {"Clip 10", "Clip 1", "", false},
        {"Clip 2", "Clip", "", false},
        {"Clip.roll", "Clip", "", false},
        {"Other", "Clip", "", false},
    };
    for (const auto& r : rows) {
        CAPTURE(r.script);
        CAPTURE(r.video);
        auto got = variant_label(r.script, r.video);
        CHECK(got.first == r.label);
        CHECK(got.second == r.ok);
    }
}

TEST_CASE("normalize_title") {
    CHECK(normalize_title("Clip (2019) [1080p] x265.mp4") == "clip mp4");
    CHECK(normalize_title("My.Film_Name-HEVC") == "my film name");
}

TEST_CASE("split_folders") {
    CHECK(split_folders(" /a \n\n/b\n/a\n") == std::vector<std::string>{"/a", "/b"});
}

TEST_CASE("walk depth limit") {
    TempDir t;
    fs::path ok = t.path / "a" / "b" / "c" / "d" / "e" / "in.funscript";       // 5 levels below root
    fs::path deep = t.path / "a" / "b" / "c" / "d" / "e" / "f" / "out.funscript";  // 6 levels: skipped
    touch(ok);
    touch(deep);
    std::vector<std::string> found;
    walk_script_folder(t.path.string(), [&](const std::string& p) { found.push_back(p); });
    REQUIRE(found.size() == 1);
    CHECK(name_of(found[0]) == "in.funscript");
}

TEST_CASE("walk file limit") {
    TempDir t;
    for (int i = 0; i < 6; ++i) touch(t.path / ("f" + std::to_string(i) + ".funscript"));
    std::vector<std::string> found;
    walk_script_folder(t.path.string(), [&](const std::string& p) { found.push_back(p); }, 3);
    CHECK(found.size() == 3);
}
