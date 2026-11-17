// samn_goldenclip: cuts golden clips out of a long video. It measures where
// the movement is (frame differences at low resolution, plus the travel of a
// script when one is given), then lets ffmpeg cut
//   1. the three minutes with the most movement, and
//   2. where it is worth it, a clip of dense movement of at most 25 MB
//      (a stream copy when enough seconds fit, else a re-encode under the cap),
// each with its script cut to the same window and moved to start at 0, and
// writes golden_manifest.json (source, start, duration, movement, size).
// The clips stay on the user's disk; nothing goes into a repository.
//
//   samn_goldenclip <video> [--script <file.funscript>] [--out <dir>]
//                   [--long-s 180] [--max-mb 25] [--dense-s 60] [--min-motion 0.5]
//                   [--probe-every-s N] [--ffmpeg <program>] [--ffprobe <program>] [--dry-run]

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "golden_clip.h"

namespace fs = std::filesystem;
using namespace samn::golden;

namespace {

const char* kHelp = R"(samn_goldenclip: golden clips from a long video (ffmpeg does the cutting)

usage: samn_goldenclip <video> [--script <file.funscript>] [--out <dir>] [--long-s N] [--max-mb X]
                       [--dense-s N] [--min-motion X] [--probe-every-s N]
                       [--ffmpeg <program>] [--ffprobe <program>] [--dry-run] [--help]

  --script         the script of the video: its stroke travel counts as movement, and
                   it is cut alongside each clip, moved to start at 0
                   (default: <video name>.funscript beside the video, when it is there)
  --out            output folder (default: <video name>_golden beside the video)
  --long-s         length of the long clip in seconds (default 180)
  --max-mb         size cap of the dense clip in MB (default 25)
  --dense-s        length of the dense clip when it is re-encoded, and the most a
                   stream copy takes (default 60)
  --min-motion     below this movement value the dense clip is not cut (default 0.5)
  --probe-every-s  measure one second every N seconds by seeking instead of decoding
                   the whole file (faster on long 4K files, coarser)
  --ffmpeg, --ffprobe   the programs (default: ffmpeg and ffprobe on the PATH, or the
                   environment variables SAMN_FFMPEG and SAMN_FFPROBE)
  --dry-run        measure and print the plan, cut nothing

Writes <out>/<name>_long<ext>, <out>/<name>_dense<ext or .mp4>, their .funscript files
and <out>/golden_manifest.json. A stream copy starts on the key frame at or before
the chosen second; the manifest and the script use that real start.
)";

std::string Fixed(double v, int digits) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}

nlohmann::json WindowJson(const Window& w) {
    return {{"chosen_start_s", w.start_s},
            {"chosen_duration_s", w.duration_s},
            {"score", std::round(w.score * 1000) / 1000},
            {"motion", std::round(w.motion * 1000) / 1000},
            {"script_travel_per_s", std::round(w.activity * 10) / 10}};
}

}  // namespace

int main(int argc, char** argv) {
    fs::path video, script, out;
    PlanOptions plan_options;
    MotionOptions motion_options;
    Tools tools;
    if (const char* e = std::getenv("SAMN_FFMPEG")) tools.ffmpeg = e;
    if (const char* e = std::getenv("SAMN_FFPROBE")) tools.ffprobe = e;
    bool dry_run = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "samn_goldenclip: " << a << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            std::cout << kHelp;
            return 0;
        } else if (a == "--script") script = fs::u8path(next());
        else if (a == "--out") out = fs::u8path(next());
        else if (a == "--long-s") plan_options.long_s = std::max(1, std::atoi(next().c_str()));
        else if (a == "--max-mb") plan_options.max_mb = std::max(0.1, std::atof(next().c_str()));
        else if (a == "--dense-s") plan_options.dense_s = std::max(1, std::atoi(next().c_str()));
        else if (a == "--min-motion") plan_options.min_motion = std::atof(next().c_str());
        else if (a == "--probe-every-s") motion_options.probe_every_s = std::atoi(next().c_str());
        else if (a == "--ffmpeg") tools.ffmpeg = next();
        else if (a == "--ffprobe") tools.ffprobe = next();
        else if (a == "--dry-run") dry_run = true;
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "samn_goldenclip: unknown option " << a << "\n" << kHelp;
            return 2;
        } else if (video.empty()) video = fs::u8path(a);
        else {
            std::cerr << "samn_goldenclip: one video at a time (got " << a << " as well)\n";
            return 2;
        }
    }
    if (video.empty()) {
        std::cerr << kHelp;
        return 2;
    }
    plan_options.min_dense_s = std::min(plan_options.min_dense_s, plan_options.dense_s);
    std::error_code ec;
    if (!fs::exists(video, ec)) {
        std::cerr << "samn_goldenclip: no such video: " << video.u8string() << "\n";
        return 2;
    }
    if (script.empty()) {
        fs::path beside = video;
        beside.replace_extension(".funscript");
        if (fs::exists(beside, ec)) script = beside;
    }
    if (out.empty()) out = video.parent_path() / (video.stem().u8string() + "_golden");
    if (!dry_run && !HaveProgram(tools.ffmpeg)) {
        std::cerr << "samn_goldenclip: the ffmpeg program does not run (" << tools.ffmpeg
                  << "); install it, or name it with --ffmpeg, or use --dry-run for the plan alone\n";
        return 2;
    }

    std::vector<samn::Action> actions;
    if (!script.empty()) {
        const auto text = samn::ReadFileToString(script);
        try {
            if (!text) throw std::runtime_error("cannot read it");
            actions = samn::Parse(*text).actions;
        } catch (const std::exception& e) {
            std::cerr << "samn_goldenclip: the script cannot be used: " << e.what() << "\n";
            return 2;
        }
    }

    std::cerr << "measuring the movement ...\n";
    int last_percent = -1;
    motion_options.progress = [&](double share) {
        const int percent = int(share * 100);
        if (percent / 10 != last_percent / 10) std::cerr << "  " << percent << " %\n";
        last_percent = percent;
    };
    std::string error;
    const auto series = MeasureMotion(video, motion_options, &error);
    if (!series) {
        std::cerr << "samn_goldenclip: " << error << "\n";
        return 1;
    }
    const std::vector<double> activity =
        actions.empty() ? std::vector<double>{} : ScriptActivity(actions, series->motion.size());
    const std::uintmax_t bytes = fs::file_size(video, ec);
    const Plan plan = PlanClips(*series, activity, bytes, plan_options);

    std::cout << "video: " << Fixed(series->duration_s, 1) << " s, " << series->width << "x" << series->height << ", "
              << Fixed(bytes / 1048576.0, 1) << " MB, movement p50 " << Fixed(PercentileOf(series->motion, 50), 2)
              << " / p95 " << Fixed(PercentileOf(series->motion, 95), 2) << (actions.empty() ? ", no script" : ", with its script")
              << "\n";
    std::cout << "long clip:  from " << Fixed(plan.long_clip.start_s, 0) << " s for " << Fixed(plan.long_clip.duration_s, 0)
              << " s, movement " << Fixed(plan.long_clip.motion, 2) << ", score " << Fixed(plan.long_clip.score, 2) << "\n";
    if (plan.dense)
        std::cout << "dense clip: from " << Fixed(plan.dense_clip.start_s, 0) << " s for "
                  << Fixed(plan.dense_clip.duration_s, 0) << " s, movement " << Fixed(plan.dense_clip.motion, 2)
                  << ", score " << Fixed(plan.dense_clip.score, 2) << " (" << plan.dense_reason << ")\n";
    else
        std::cout << "dense clip: not cut (" << plan.dense_reason << ")\n";

    nlohmann::json manifest = {{"tool", "samn_goldenclip"},
                               {"source", video.filename().u8string()},
                               {"source_bytes", bytes},
                               {"source_duration_s", std::round(series->duration_s * 1000) / 1000},
                               {"source_size", {series->width, series->height}},
                               {"script", script.empty() ? nlohmann::json() : nlohmann::json(script.filename().u8string())},
                               {"movement", "median over each second of the mean absolute difference of 160 px grey "
                                            "pictures 0.25 s apart (0-255); score = that against its 95th percentile, "
                                            "averaged with the script's travel when a script is given"},
                               {"movement_p50", std::round(PercentileOf(series->motion, 50) * 1000) / 1000},
                               {"movement_p95", std::round(PercentileOf(series->motion, 95) * 1000) / 1000},
                               {"probe_every_s", motion_options.probe_every_s},
                               {"dense_note", plan.dense_reason},
                               {"clips", nlohmann::json::array()}};
    if (dry_run) {
        manifest["dry_run"] = true;
        manifest["clips"].push_back({{"kind", "long"}, {"window", WindowJson(plan.long_clip)}});
        if (plan.dense) manifest["clips"].push_back({{"kind", "dense"}, {"window", WindowJson(plan.dense_clip)}});
        std::cout << manifest.dump(2) << "\n";
        return 0;
    }

    fs::create_directories(out);
    const std::string stem = video.stem().u8string();
    const std::string ext = video.extension().u8string();
    int failed = 0;
    auto record = [&](const char* kind, const Window& w, const CutResult& r, const fs::path& file) {
        nlohmann::json j = {{"kind", kind}, {"window", WindowJson(w)}};
        if (!r.ok) {
            ++failed;
            j["error"] = r.error;
            std::cerr << "samn_goldenclip: the " << kind << " clip failed: " << r.error << "\n";
        } else {
            j["file"] = file.filename().u8string();
            j["mode"] = r.mode;
            j["start_s"] = std::round(r.start_s * 1000) / 1000;
            j["duration_s"] = std::round(r.duration_s * 1000) / 1000;
            j["bytes"] = r.bytes;
            if (!script.empty()) {
                fs::path cut_script = file;
                cut_script.replace_extension(".funscript");
                bool written = false;
                std::string script_error;
                if (!WriteCutScript(script, cut_script, r.start_s, r.duration_s, &written, &script_error)) {
                    ++failed;
                    j["script_error"] = script_error;
                } else if (written) {
                    j["script"] = cut_script.filename().u8string();
                } else {
                    j["script_note"] = "the script has no actions in this window";
                }
            }
            std::cout << kind << ": " << file.u8string() << " (" << r.mode << ", from " << Fixed(r.start_s, 3) << " s for "
                      << Fixed(r.duration_s, 3) << " s, " << Fixed(r.bytes / 1048576.0, 1) << " MB)\n";
        }
        manifest["clips"].push_back(j);
    };

    std::cerr << "cutting the long clip ...\n";
    const fs::path long_file = out / (stem + "_long" + ext);
    CutResult long_cut = CutCopy(tools, video, long_file, plan.long_clip.start_s, plan.long_clip.duration_s);
    fs::path long_written = long_file;
    if (!long_cut.ok) {
        // No key frame list (no ffprobe) or a container that cannot be copied: a re-encode at the
        // source's own average rate, so the long clip is still there.
        const double kbps = series->duration_s > 0 ? bytes * 8 / 1000.0 / series->duration_s : 4000;
        long_written = out / (stem + "_long.mp4");
        long_cut = CutReencode(tools, video, long_written, plan.long_clip.start_s, plan.long_clip.duration_s,
                               std::max(300, int(kbps)), plan_options.audio_kbps, 100000);
    }
    record("long", plan.long_clip, long_cut, long_written);

    if (plan.dense) {
        std::cerr << "cutting the dense clip ...\n";
        const fs::path copy_file = out / (stem + "_dense" + ext), reencode_file = out / (stem + "_dense.mp4");
        const CutResult dense_cut = CutUnderCap(tools, video, copy_file, reencode_file, plan, plan_options);
        record("dense", plan.dense_clip, dense_cut, dense_cut.mode == "copy" ? copy_file : reencode_file);
    }
    samn::WriteFileAtomic(out / "golden_manifest.json", manifest.dump(2) + "\n");
    std::cout << "manifest: " << (out / "golden_manifest.json").u8string() << "\n";
    return failed ? 1 : 0;
}
