// Memory and speed of label tracking between two keys (docs/benchmarks/LABEL_MEMORY_2026-11-08.md).
//   samn_label_mem --make clip.mp4 [seconds]   writes a synthetic clip (a disc on a texture; no media)
//   samn_label_mem --track clip.mp4 [seconds [box_scale]]  tracks it between two keys, prints peak RSS, wall and CPU time, a hash
// Any other clip (a long-GOP H.264 re-encode of the synthetic one, for instance) works with --track.
// The two steps run as two processes so that the clip writer does not count in the peak.
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "samn/engine/label_tracking.h"
#include "synthetic_clips.h"

namespace {

constexpr int kBigW = 1280, kBigH = 720, kRadius = 70;
constexpr double kAmp = 200.0, kFreq = 0.25;

double CenterY(double seconds) { return kBigH / 2.0 + kAmp / 2 * std::sin(2 * samn::engine::testclips::kPi * kFreq * seconds); }

double g_box_scale = 1.0;  // a clip re-encoded larger than 1280x720: the box scales with it

samn::LabelBox DiscAt(int64_t ms) {
    const double y = CenterY(double(ms) / 1000.0);
    return {g_box_scale * (kBigW / 2.0 - kRadius), g_box_scale * (y - kRadius), g_box_scale * 2.0 * kRadius,
            g_box_scale * 2.0 * kRadius};
}

bool Make(const std::filesystem::path& p, int seconds) {
    using namespace samn::engine::testclips;
    cv::VideoWriter w(p.u8string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), kFps, {kBigW, kBigH}, true);
    if (!w.isOpened()) return false;
    const cv::Mat bg = Texture(kBigW, kBigH, 11, 700);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        cv::Mat f = bg.clone();
        cv::circle(f, {kBigW / 2, int(CenterY(i / kFps))}, kRadius, cv::Scalar(250, 250, 250), cv::FILLED);
        w.write(f);
    }
    return true;
}

// Peak resident set of this process in kB (Linux; 0 elsewhere).
long PeakRssKb() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("VmHWM:", 0) == 0) return std::atol(line.c_str() + 6);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: samn_label_mem --make|--track clip.mp4 [seconds]\n");
        return 2;
    }
    const std::string mode = argv[1];
    const std::filesystem::path clip = argv[2];
    const int seconds = argc > 3 ? std::atoi(argv[3]) : 40;
    if (argc > 4) g_box_scale = std::atof(argv[4]);
    if (mode == "--make") return Make(clip, seconds) ? 0 : 1;
    if (mode != "--track") return 2;

    samn::engine::BetweenKeysOptions o;
    o.max_frames = 100000;
    samn::engine::BetweenKeysStats stats;
    o.stats = &stats;
    std::string error;
    const int64_t to_ms = int64_t(seconds) * 1000 - 100;
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = samn::engine::TrackBetweenKeys(clip, 0, DiscAt(0), to_ms, DiscAt(to_ms), o, &error);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!r) {
        std::fprintf(stderr, "tracking failed: %s\n", error.c_str());
        return 1;
    }
    // FNV-1a over the exact bits of every tracked box: equal hashes, equal output.
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](double v) {
        uint64_t b;
        std::memcpy(&b, &v, sizeof b);
        for (int i = 0; i < 8; ++i) h = (h ^ ((b >> (8 * i)) & 0xff)) * 1099511628211ull;
    };
    for (const auto* run : {&r->forward, &r->backward})
        for (const auto& s : *run) {
            mix(double(s.ms));
            mix(s.box ? 1.0 : 0.0);
            if (s.box) { mix(s.box->x); mix(s.box->y); mix(s.box->w); mix(s.box->h); }
        }
    const int frames = int(std::llround(double(seconds) * samn::engine::testclips::kFps));
    std::printf("samples=%zu/%zu frames=%d peak_rss_mb=%.1f wall_ms=%.0f ms_per_frame=%.2f cpu_ms_per_frame=%.2f "
                "hash=%016llx window=%d peak_frames=%d grabbed=%d\n",
                r->forward.size(), r->backward.size(), frames, PeakRssKb() / 1024.0, ms, ms / frames,
                1000.0 * double(std::clock()) / CLOCKS_PER_SEC / frames, static_cast<unsigned long long>(h),
                stats.window_used, stats.peak_frames_held.load(), stats.frames_decoded);
    return 0;
}
