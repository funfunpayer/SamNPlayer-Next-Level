#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "samn/fileio.h"

namespace fs = std::filesystem;

namespace {
std::string Read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
}  // namespace

TEST_CASE("fileio: atomic write creates, replaces and leaves no temp file") {
    const fs::path dir = fs::temp_directory_path() / ("samn_fileio_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path f = dir / "Übung.json";
    samn::WriteFileAtomic(f, "one");
    CHECK(Read(f) == "one");
    samn::WriteFileAtomic(f, std::string(3 << 20, 'x'));  // bigger than one write chunk
    CHECK(fs::file_size(f) == (3u << 20));
    samn::WriteFileAtomic(f, "");
    CHECK(Read(f).empty());
    int files = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        (void)e;
        ++files;
    }
    CHECK(files == 1);
    // A missing folder fails with the path in the message; nothing is created.
    try {
        samn::WriteFileAtomic(dir / "missing" / "x.json", "x");
        FAIL("expected an error");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("x.json") != std::string::npos);
    }
    CHECK_FALSE(fs::exists(dir / "missing"));
    fs::remove_all(dir);
}

// 0.7 funscript/atomic_write_test.go TestWriteFileAtomicCleansTempAfterRenameFailure
// (and its generator/native_write_test.go twin): a failed rename throws,
// leaves the destination as it was and leaves no temp file behind.
TEST_CASE("fileio: a failed rename leaves the destination and the folder clean") {
    const fs::path dir = fs::temp_directory_path() / ("samn_fileio_" + std::to_string(std::random_device{}()));
    const fs::path dest = dir / "target.json";
    fs::create_directories(dest);  // a non-empty folder cannot be replaced by a file
    std::ofstream(dest / "keep.txt") << "keep";
    CHECK_THROWS_AS(samn::WriteFileAtomic(dest, "new"), std::runtime_error);
    CHECK(fs::is_directory(dest));
    CHECK(Read(dest / "keep.txt") == "keep");
    int entries = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        CHECK_MESSAGE(e.path().filename() == "target.json", "left behind: " << e.path().filename().string());
        ++entries;
    }
    CHECK(entries == 1);
    fs::remove_all(dir);
}
