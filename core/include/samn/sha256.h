#pragma once
// SHA-256 (FIPS 180-4), small and dependency-free: content hashes for
// benchmark manifests (GD-05: candidates are identified by content, not by
// path).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace samn {

class Sha256 {
public:
    Sha256();
    void Update(const void* data, size_t len);
    std::string HexDigest();  // finishes; call once

private:
    void Block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t buf_len_ = 0;
    uint64_t total_ = 0;
};

std::string Sha256Hex(const std::string& data);
// nullopt when the file cannot be read.
std::optional<std::string> Sha256File(const std::filesystem::path& path);

}  // namespace samn
