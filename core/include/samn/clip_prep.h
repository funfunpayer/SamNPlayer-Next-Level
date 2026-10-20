#pragma once
// Clip prep (0.7's Benchmark "Clip-Prep" and scripts/benchmark-prep, D-155
// step 6): short, downscaled cuts of long videos for the benchmark, golden
// clips and test media. The cut is made by ffmpeg with 0.7's settings:
// Lanczos down to a maximum width (never up), H.264 CRF 20 "veryfast",
// yuv420p, AAC 128k or no sound, +faststart. A script beside the video is
// cut to the same window.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

// Seconds ("85", "85.5") or MM:SS / HH:MM:SS with optional fractions;
// nullopt for anything else or a negative time.
std::optional<double> ParseClipTime(const std::string& text);

// Seconds for ffmpeg's -ss / -t: whole seconds bare, else up to 3 decimals.
std::string FormatClipSeconds(double seconds);

// "720p" 1280, "960w" 960, "1080p" 1920; anything else 1280.
int ClipPrepMaxWidth(const std::string& preset);

struct ClipPrepOptions {
    std::string input, output;
    double start_s = 0, end_s = 0;
    int max_width = 1280;
    bool no_audio = false;
    int crf = 20;
};

// ffmpeg's arguments (without the program), with -progress on stdout so the
// caller sees how far it is; nullopt with *error for a bad request.
std::optional<std::vector<std::string>> ClipPrepArgs(const ClipPrepOptions& o, std::string* error = nullptr);

// The file name 0.7 suggested beside the source: <stem>_<start>-<end>.mp4,
// the times as typed with ':' as '-'.
std::string SuggestClipName(const std::string& source_stem, const std::string& start, const std::string& end);

// A batch of cuts (0.7's marks.json): {"source", "max_width", "clips":
// [{"name", "start", "end"} or {"name", "start_sec", "duration_sec"}]}.
struct ClipMark {
    std::string name;
    double start_s = 0, end_s = 0;
};
struct ClipMarks {
    std::string source;
    int max_width = 1280;
    std::vector<ClipMark> clips;
};
std::optional<ClipMarks> ParseClipMarks(const std::string& json, std::string* error = nullptr);

// The script's actions inside [start_ms, end_ms], moved to start at 0, with
// the position at both edges added where the stroke crosses them.
std::vector<Action> CutScriptWindow(const std::vector<Action>& actions, int64_t start_ms, int64_t end_ms);

}  // namespace samn
