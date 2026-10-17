#pragma once

// Synthetic test clips with known ground truth (GD-22), the same shapes as
// 0.7's trackcv tests: a bright disc moving on a textured background, with
// a camera pan or a hard cut. Generated in the test; no media in the repo.

#include <cmath>
#include <filesystem>
#include <random>
#include <string>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace samn::engine::testclips {

constexpr int kW = 320, kH = 240;
constexpr double kFps = 25.0;
constexpr double kPi = 3.14159265358979323846;

inline cv::Mat Texture(int w, int h, unsigned seed, int boxes) {
    cv::Mat m(h, w, CV_8UC3, cv::Scalar(40, 40, 40));
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    for (int i = 0; i < boxes; ++i) {
        const int x = pick(w), y = pick(h), bw = 6 + pick(12), bh = 6 + pick(12);
        const cv::Scalar c(60 + pick(130), 60 + pick(130), 60 + pick(130));
        cv::rectangle(m, cv::Rect(x, y, bw, bh) & cv::Rect(0, 0, w, h), c, cv::FILLED);
    }
    return m;
}

inline void Disc(cv::Mat& f, int cx, int cy) { cv::circle(f, {cx, cy}, 35, cv::Scalar(250, 250, 250), cv::FILLED); }

inline cv::VideoWriter Writer(const std::filesystem::path& p) {
    return cv::VideoWriter(p.u8string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), kFps, {kW, kH}, true);
}

// Disc at x 160 moving y = 120 + amp/2 sin(2 pi f t).
inline bool WriteMoving(const std::filesystem::path& p, int seconds, double amp, double freq) {
    auto w = Writer(p);
    if (!w.isOpened()) return false;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        cv::Mat f = bg.clone();
        Disc(f, 160, int(120 + amp / 2 * std::sin(2 * kPi * freq * i / kFps)));
        w.write(f);
    }
    return true;
}

// The camera window pans over a larger world while the disc moves 1 Hz.
inline bool WritePanning(const std::filesystem::path& p, int seconds, double amp) {
    auto w = Writer(p);
    if (!w.isOpened()) return false;
    const int pad = 60;
    const cv::Mat world = Texture(kW + 2 * pad, kH + 2 * pad, 11, 260);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        const double t = i / kFps;
        const int ox = int(60 + 30 * std::sin(2 * kPi * t * 0.11));
        const int oy = int(60 + 20 * std::sin(2 * kPi * t * 0.08));
        cv::Mat f = world(cv::Rect(ox, oy, kW, kH)).clone();
        Disc(f, 160, int(60 + 120 + amp / 2 * std::sin(2 * kPi * t)) - oy);
        w.write(f);
    }
    return true;
}

// A hard cut to another, darker background at frame cut_at.
inline bool WriteHardCut(const std::filesystem::path& p, int seconds, int cut_at, double amp) {
    auto w = Writer(p);
    if (!w.isOpened()) return false;
    const cv::Mat bg1 = Texture(kW, kH, 11, 140);
    cv::Mat bg2 = Texture(kW, kH, 99, 140);
    bg2.convertTo(bg2, -1, 0.4, 12);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        cv::Mat f = (i >= cut_at ? bg2 : bg1).clone();
        Disc(f, 160, int(120 + amp / 2 * std::sin(2 * kPi * i / kFps)));
        w.write(f);
    }
    return true;
}

// The same scene, then at cut_at a lighting cut (whole frame darker) with the
// disc 120 px further right: reacquire has to find it.
inline bool WriteLightingCutJump(const std::filesystem::path& p, int seconds, int cut_at) {
    auto w = Writer(p);
    if (!w.isOpened()) return false;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        cv::Mat f = bg.clone();
        Disc(f, i >= cut_at ? 280 : 160, int(120 + 10 * std::sin(2 * kPi * i / kFps)));
        if (i >= cut_at) f.convertTo(f, -1, 0.4, 12);
        w.write(f);
    }
    return true;
}

}  // namespace samn::engine::testclips
