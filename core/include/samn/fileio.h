#pragma once

// File writing shared by every module that saves user data.

#include <filesystem>
#include <string>

namespace samn {

// Write data to path atomically: a temp file in the same folder, flushed to
// disk, then renamed over the target. A crash or a full disk never leaves a
// half-written file; the old content stays until the new one is complete.
// New files get the default permissions (0644 minus umask). Throws
// std::runtime_error naming the path on failure; the temp file is removed.
// (Go writeFileAtomic; before, marks, .samn and settings each had a copy.)
void WriteFileAtomic(const std::filesystem::path& path, const std::string& data);

}  // namespace samn
