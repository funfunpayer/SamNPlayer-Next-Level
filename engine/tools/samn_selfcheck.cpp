// samn_selfcheck: the self-checking engine on the command line
// (docs/P3_SELF_CHECK_ENGINE.md). Tracks one box with the checker signals
// recorded, prints the window verdicts and, with --repair, runs the verified
// repair. Writes numbers only (CSV), never media.
//
//   samn_selfcheck <video> <x> <y> <w> <h> [options]
//     --start-ms N        start of the track (the box's frame), default 0
//     --window N          window length in frames, default 60 (2 s at 30 fps)
//     --skip N            frames at the start without a verdict, default 90
//     --adaptive-cuts     AdaptiveSceneCut instead of 0.7's fixed thresholds
//     --axis x|y          force the axis (default: automatic)
//     --repair            run RepairTrack on the failed windows
//     --min-appearance F  repair acceptance, default 0.75
//     --out DIR           write track.csv and windows.csv (and repair.csv) there
//
// track.csv:   time_ms, position, x, y (box centre, frame pixels)
// windows.csv: first, end, state, c1, c2, c3, c4, c6, c7, c8, c9, reasons
// repair.csv:  time_ms, position after repair (empty in gaps)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "samn/engine/repair.h"
#include "samn/engine/self_check.h"
#include "samn/engine/track_box.h"

namespace {

std::string Num(double v) {
    if (!std::isfinite(v)) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

std::string Csv(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace samn::engine;
    if (argc < 6) {
        std::cerr << "usage: samn_selfcheck <video> <x> <y> <w> <h> [--start-ms N] [--window N] [--skip N]\n"
                     "       [--adaptive-cuts] [--axis x|y] [--repair] [--min-appearance F] [--out DIR]\n";
        return 2;
    }
    const std::filesystem::path video = std::filesystem::u8path(argv[1]);
    const cv::Rect box(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));

    TrackOptions track;
    track.record_checks = true;
    VerifyOptions verify;
    verify.window_frames = 60;
    verify.skip_frames = 90;
    bool repair = false;
    double min_appearance = 0.75;
    std::filesystem::path out;
    for (int i = 6; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "samn_selfcheck: " << a << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--start-ms") track.start_ms = std::atoll(next().c_str());
        else if (a == "--window") verify.window_frames = std::atoi(next().c_str());
        else if (a == "--skip") verify.skip_frames = std::atoi(next().c_str());
        else if (a == "--adaptive-cuts") track.adaptive_cuts = true;
        else if (a == "--axis") track.axis = next() == "x" ? TrackAxis::kX : TrackAxis::kY;
        else if (a == "--repair") repair = true;
        else if (a == "--min-appearance") min_appearance = std::atof(next().c_str());
        else if (a == "--out") out = std::filesystem::u8path(next());
        else {
            std::cerr << "samn_selfcheck: unknown option " << a << "\n";
            return 2;
        }
    }

    std::string error;
    const auto first = TrackBox(video, box, track, &error);
    if (!first) {
        std::cerr << "samn_selfcheck: " << error << "\n";
        return 1;
    }
    VerifyOptions vo = verify;
    vo.cuts = first->scene_cuts;
    const auto verdicts = VerifyWindows(first->positions, first->checks, vo);

    int counts[4] = {0, 0, 0, 0};
    for (const auto& v : verdicts) ++counts[static_cast<int>(v.state)];
    std::cout << "frames " << first->positions.size() << ", axis " << (first->horizontal ? "x" : "y") << ", cuts "
              << first->scene_cuts.size() << "\nwindows " << verdicts.size() << ": pass " << counts[0] << ", suspect "
              << counts[1] << ", fail " << counts[2] << ", still " << counts[3] << "\n";

    if (!out.empty()) {
        std::filesystem::create_directories(out);
        std::ofstream t(out / "track.csv");
        t << "time_ms,position,x,y\n";
        for (size_t i = 0; i < first->positions.size(); ++i)
            t << first->times_ms[i] << ',' << Num(first->positions[i]) << ',' << Num(first->xs[i]) << ','
              << Num(first->frame_ys[i]) << '\n';
        std::ofstream w(out / "windows.csv");
        w << "first,end,state,c1,c2,c3,c4,c6,c7,c8,c9,reasons\n";
        for (const auto& v : verdicts)
            w << v.first << ',' << v.end << ',' << WindowStateName(v.state) << ',' << Num(v.c1) << ',' << Num(v.c2)
              << ',' << Num(v.c3) << ',' << Num(v.c4) << ',' << Num(v.c6) << ',' << Num(v.c7) << ',' << Num(v.c8)
              << ',' << Num(v.c9) << ',' << Csv(v.reasons) << '\n';
    }

    if (repair) {
        RepairOptions o;
        o.track = track;
        o.seed_box = box;
        o.verify = verify;
        o.min_appearance = min_appearance;
        o.on_progress = [](int done, int total) { std::cerr << "repair " << done << "/" << total << "\n"; };
        const auto r = RepairTrack(video, *first, o, &error);
        if (!r) {
            std::cerr << "samn_selfcheck: " << error << "\n";
            return 1;
        }
        std::cout << "repair: failed " << r->failed << ", repaired " << r->repaired << ", gaps " << r->gaps << "\n";
        if (!out.empty()) {
            std::ofstream f(out / "repair.csv");
            f << "time_ms,position\n";
            for (size_t i = 0; i < r->positions.size(); ++i) f << first->times_ms[i] << ',' << Num(r->positions[i]) << '\n';
        }
    }
    return 0;
}
