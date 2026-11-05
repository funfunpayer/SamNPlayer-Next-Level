#pragma once

// File reading and writing shared by every module that loads or saves user data.

#include <filesystem>
#include <optional>
#include <string>

namespace samn {

// Write data to path atomically: a temp file in the same folder, flushed to
// disk, then renamed over the target. A crash or a full disk never leaves a
// half-written file; the old content stays until the new one is complete.
// New files get the default permissions (0644 minus umask). Throws
// std::runtime_error naming the path on failure; the temp file is removed.
// (Go writeFileAtomic; before, marks, .samn and settings each had a copy.)
void WriteFileAtomic(const std::filesystem::path& path, const std::string& data);

// The whole file as bytes (binary mode, no newline translation). nullopt when
// it cannot be opened or a read fails; the callers word their own errors.
std::optional<std::string> ReadFileToString(const std::filesystem::path& path);

}  // namespace samn
