#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "samn/sha256.h"

// FIPS 180-4 / NIST test vectors.
TEST_CASE("sha256: known vectors") {
    CHECK(samn::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(samn::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(samn::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(samn::Sha256Hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: a file hashes like its content; a missing file is nullopt") {
    const auto p = std::filesystem::temp_directory_path() / "samn_sha256_test.bin";
    {
        std::ofstream(p, std::ios::binary) << std::string(70000, 'x');
    }
    CHECK(samn::Sha256File(p) == samn::Sha256Hex(std::string(70000, 'x')));
    std::filesystem::remove(p);
    CHECK(!samn::Sha256File(p));
}
