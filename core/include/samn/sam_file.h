#pragma once

// Reader for the 0.7 `.sam` sidecar (Go package sam, sam/sam.go + sidecar.go).
// READ-ONLY by decision D-170: SamNPlayer 2 imports an existing `<script>.sam`
// and never writes one.
//
// The 0.7 file is one JSON document:
//   {"version": "0.1",
//    "frames": [{"time": <ms>, "motion": {"position": 0-100, "intensity": 0-1,
//                "confidence": 0-1, ...}}, ...],
//    "metadata": {"creator", "source", "profile", "device_recipe",
//                 "tracking_gaps": [{"start_ms", "end_ms"}]}}
// Only `version`, `frames[].time`, `frames[].motion.intensity` and the three
// metadata strings are read; every other field is skipped. Details and the
// mapping into SamNPlayer 2 are in docs/PARITY.md section 9 and D-170.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// Limits (0.7 had none): a larger or deeper file is refused, not truncated.
constexpr size_t kSamMaxBytes = 64u * 1024 * 1024;
constexpr size_t kSamMaxFrames = 1000000;
constexpr size_t kSamMaxDepth = 32;
constexpr size_t kSamMaxErrorChars = 200;  // an error never echoes the file

struct SamFrame {
    int64_t time_ms = 0;     // "time", default 0 when missing
    double intensity = 0;    // motion.intensity, clamped to 0..1, default 0
};

struct SamFile {
    std::string version;   // required, any non-empty text
    std::string creator;
    std::string source;
    std::string profile;
    std::vector<SamFrame> frames;  // sorted by time (stable), never empty
    size_t dropped = 0;            // frames with a negative time, left out
};

struct SamReadResult {
    std::optional<SamFile> file;  // set on success
    std::string error;            // set on failure: one English sentence
};

// Deviations from Go's encoding/json (0.7 sam.Parse), all deliberate:
//  - a UTF-8 BOM is skipped (Go rejects it); a NUL byte is refused;
//  - a float `time` is rounded to the nearest millisecond (Go rejects 1500.5
//    for int64); |time| of 9e15 or more is refused;
//  - a number that overflows (1e999) refuses the file; underflow (1e-400) is 0;
//  - keys are case-sensitive (Go matches them case-insensitively);
//  - a repeated "frames" key appends (Go keeps the last one's elements);
//  - frames with a negative time are dropped (counted in `dropped`), and a
//    frame without `time` has time 0 (as Go's zero value);
//  - strings are cut to 256 characters; errors are at most 200 characters and
//    never quote the file.
// Parses a .sam document (UTF-8, a leading BOM and CRLF are accepted). Never
// throws; bad, oversized, binary, deeply nested or non-finite input gives an
// error. A field of the wrong type is an error naming the field, as in 0.7;
// null counts as missing; unknown fields are skipped.
SamReadResult ParseSam(std::string_view data);

// LoadSam with a one-entry cache keyed by path, size and mtime (a script
// reloaded after undo or a range edit does not parse again). Thread safe.
SamReadResult LoadSamCached(const std::filesystem::path& path);

// Reads and parses `path`; a missing or unreadable file gives an error.
SamReadResult LoadSam(const std::filesystem::path& path);

// The sidecar of a script (`.funscript` or `.samn`): same folder and stem,
// extension `.sam` (Go SidecarPath). Empty for an empty path.
std::string SamSidecarPath(const std::string& script_path);

// True when some frame carries intensity above 0.001 (Go HasContactIntensity):
// a position-only file is not worth importing.
bool SamHasContactIntensity(const SamFile& f);

// The intensity as a lane: pos = round(intensity * 100), one point per
// distinct time (the later frame wins), times ascending.
std::vector<Action> SamIntensityLane(const SamFile& f);

}  // namespace samn
