// samn_tip_bench: the automatic tip box on POV-like synthetic scenes
// (tests/engine/tip_scenes.h): where FindTip puts the box, and how well
// TrackBox follows the mover from it. Numbers only; every scene is a drawn
// shape, never real media. By hand, not in ctest (times depend on the
// machine); the tests in tests/engine/tip_auto_test.cpp pin the behaviour.
//
//   samn_tip_bench [--scenes a,b,...] [--out <dir>] [--vertical] [--list]
//
// --vertical scores the vertical motion only (TipAxis::kVertical, the finder
// before the main-direction score): the "before" column of the tables.
//
// Per scene, three start boxes are tracked with the Create settings (15 fps
// cap, camera compensation, scene cuts, appearance memory):
//   tip      FindTip's compact proposal (TipFindResult::box)
//   region   FindTip's box around the whole moving region (TipFindResult::region)
//   oracle   the mover's true box at the first frame (the upper bound)
// and `tip` / `region` once more with the Generator's overlay settings:
// /old = 8 fps, fixed scene-cut test, no appearance memory (before this
// change), /pv = PreviewTrackOptions().
//
// Metrics:
//   iou        the start box against the mover's true box at the first frame
//   hit        1 when at least half of the box lies on what the mover covers and its centre is on it
//   score      FindTip's best candidate score
//   find_ms    FindTip wall time per analysed frame
//   r          |Pearson r| of the track (chosen axis, camera removed) against the true stroke on that axis
//   p95_px     p95 over frames of |box centre - the mover's centre| after removing the first frame's offset
//   on_mover   share of frames whose box centre is inside the mover's true box
//   lost       share of frames flagged lost
//   cuts       scene cuts the engine saw (every one is false: the scenes have none)
//   ms         TrackBox wall time per frame of video

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "samn/engine/tip_find.h"
#include "samn/engine/track_box.h"
#include "tip_scenes.h"

using namespace samn::engine;
namespace fs = std::filesystem;
namespace tc = samn::engine::testclips;

namespace {

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double Pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n < 3) return 0;
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= double(n);
    mb /= double(n);
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0;
}

struct TrackScore {
    bool ok = false;
    double r = 0, p95 = 0, on_mover = 0, lost = 0, ms = 0;
    int cuts = 0;
    bool horizontal = false;
};

TrackScore Score(const fs::path& clip, const tc::TipSceneInfo& s, const cv::Rect& box, int preview) {
    TrackScore out;
    TrackOptions to;
    if (preview == 1) {
        to.max_analysis_fps = 8;
        to.camera_compensation = false;
        to.appearance_memory = false;
    } else if (preview == 2) {
        to = PreviewTrackOptions();
        if (const char* e = std::getenv("TB_FPS")) to.max_analysis_fps = std::atof(e);
        if (const char* e = std::getenv("TB_CUTS")) to.scene_cuts = std::atoi(e) != 0;
        if (const char* e = std::getenv("TB_ADAPT")) to.adaptive_cuts = std::atoi(e) != 0;
        if (const char* e = std::getenv("TB_MEM")) to.appearance_memory = std::atoi(e) != 0;
    } else {
        to.max_analysis_fps = 15;
        to.analysis_width = 640;
    }
    std::string error;
    const double t0 = NowMs();
    const auto tr = TrackBox(clip, box, to, &error);
    const double wall = NowMs() - t0;
    if (!tr || tr->times_ms.empty()) return out;
    out.ok = true;
    out.ms = wall / std::max(1, s.frames);
    out.cuts = int(tr->scene_cuts.size());
    out.horizontal = tr->horizontal;
    std::vector<double> truth, shown, err;
    int lost = 0, on = 0;
    const size_t n = tr->times_ms.size();
    const cv::Point2d off(tr->frame_xs[0] - s.centre[0].x, tr->frame_ys[0] - s.centre[0].y);
    size_t counted = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = tr->times_ms[i] / 1000.0;
        // While the picture is covered there is nothing to follow (and 0.5 s
        // after it to find the mover again).
        if (t >= s.cover_from_s && t < s.cover_to_s + 0.5) continue;
        ++counted;
        const int f = std::clamp(int(std::lround(t * s.fps)), 0, s.frames - 1);
        truth.push_back(s.stroke(t) * (tr->horizontal ? s.dir_x : s.dir_y));
        shown.push_back(tr->positions[i]);
        const cv::Point2d c = s.centre[size_t(f)];
        err.push_back(std::hypot(tr->frame_xs[i] - off.x - c.x, tr->frame_ys[i] - off.y - c.y));
        if (tr->lost[i]) ++lost;
        if (std::abs(tr->frame_xs[i] - c.x) <= s.mover_w / 2.0 && std::abs(tr->frame_ys[i] - c.y) <= s.mover_h / 2.0) ++on;
    }
    out.r = std::abs(Pearson(shown, truth));
    std::sort(err.begin(), err.end());
    out.p95 = err[size_t(0.95 * double(err.size() - 1))];
    out.lost = double(lost) / double(std::max<size_t>(1, counted));
    out.on_mover = double(on) / double(std::max<size_t>(1, counted));
    return out;
}

bool Hit(const cv::Rect& box, const tc::TipSceneInfo& s) {
    const cv::Rect swept = s.Swept(0, s.frames) & cv::Rect(0, 0, s.w, s.h);
    const cv::Point c(box.x + box.width / 2, box.y + box.height / 2);
    return box.area() > 0 && swept.contains(c) && (box & swept).area() * 2 >= box.area();
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> only;
    fs::path out = "samn_tip_bench_out";
    bool list = false, vertical = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--vertical") vertical = true;
        else if (a == "--scenes" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            for (std::string n; std::getline(ss, n, ',');) only.push_back(n);
        } else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else {
            std::cout << "usage: samn_tip_bench [--scenes a,b,...] [--out <dir>] [--vertical] [--list]\n";
            return a == "--help" ? 0 : 2;
        }
    }
    const auto scenes = tc::TipScenes();
    if (list) {
        for (const auto& s : scenes) std::cout << s.name << ": " << s.what << "\n";
        return 0;
    }
    fs::create_directories(out / "clips");
    std::ofstream csv(out / "samn_tip_bench.csv");
    csv << "scene,box,metric,value\n";
    std::printf("%-15s %-10s %3s %4s %6s %6s | %5s %6s %8s %5s %4s %5s %s\n", "scene", "box", "hit", "iou", "score", "findms", "r",
                "p95px", "on_mover", "lost", "cuts", "ms", "axis");
    int hits = 0, counted = 0;
    for (const auto& def : scenes) {
        if (!only.empty() && std::find(only.begin(), only.end(), def.name) == only.end()) continue;
        const fs::path clip = out / "clips" / (def.name + ".mp4");
        const tc::TipSceneInfo s = tc::WriteTipScene(clip, def.scene);
        if (!s.ok) {
            std::cerr << def.name << ": the clip cannot be written\n";
            return 1;
        }
        TipFindOptions fo;
        fo.max_seconds = 12;
        if (vertical) fo.axis = TipAxis::kVertical;
        std::string error;
        const double t0 = NowMs();
        const auto tip = FindTip(clip, fo, &error);
        const double find_ms = (NowMs() - t0) / std::max(1.0, std::min(double(s.frames), 12 * s.fps) / 2);
        struct Start {
            std::string name;
            cv::Rect box;
            int preview;  // 0 Create, 1 the old overlay settings, 2 PreviewTrackOptions
        };
        std::vector<Start> starts;
        double score = 0;
        if (tip) {
            score = tip->candidates.empty() ? 0 : tip->candidates.front().score;
            starts.push_back({"tip", tip->box, 0});
            starts.push_back({"tip/old", tip->box, 1});
            starts.push_back({"tip/pv", tip->box, 2});
            if (tip->region.area() > 0) {
                starts.push_back({"region", tip->region, 0});
                starts.push_back({"region/old", tip->region, 1});
                starts.push_back({"region/pv", tip->region, 2});
            }
        } else {
            std::printf("%-15s FindTip: %s\n", def.name.c_str(), error.c_str());
        }
        starts.push_back({"oracle", s.MoverBox(0) & cv::Rect(0, 0, s.w, s.h), 0});
        ++counted;
        if (tip && Hit(tip->box, s)) ++hits;
        for (const Start& st : starts) {
            const TrackScore k = Score(clip, s, st.box, st.preview);
            const bool hit = Hit(st.box, s);
            const cv::Rect truth0 = s.MoverBox(0) & cv::Rect(0, 0, s.w, s.h);
            const double iou = double((st.box & truth0).area()) / std::max(1, (st.box | truth0).area());
            std::printf("%-15s %-10s %3d %4.2f %6.2f %6.1f | %5.3f %6.1f %8.2f %5.2f %4d %5.1f %s  [%d,%d %dx%d]\n",
                        def.name.c_str(), st.name.c_str(), int(hit), iou, score, find_ms, k.r, k.p95, k.on_mover, k.lost,
                        k.cuts, k.ms, k.horizontal ? "x" : "y", st.box.x, st.box.y, st.box.width, st.box.height);
            auto row = [&](const char* metric, double v) {
                csv << def.name << ',' << st.name << ',' << metric << ',' << v << '\n';
            };
            row("hit", hit);
            row("iou", iou);
            row("score", score);
            row("find_ms", find_ms);
            row("r", k.r);
            row("p95_px", k.p95);
            row("on_mover", k.on_mover);
            row("lost", k.lost);
            row("cuts", k.cuts);
            row("ms", k.ms);
        }
    }
    std::printf("tip hit rate: %d / %d\n", hits, counted);
    std::error_code ec;
    fs::remove_all(out / "clips", ec);
    return 0;
}
