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
