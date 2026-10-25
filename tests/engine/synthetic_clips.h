#pragma once

// Synthetic test clips with known ground truth (GD-22), the same shapes as
// 0.7's trackcv tests: a bright disc moving on a textured background, with
// a camera pan or a hard cut. Generated in the test; no media in the repo.

#include <cmath>
#include <filesystem>
#include <random>
#include <vector>
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

// Tip disc (left) moves vertically; partner disc (right) moves horizontally.
// Ground for G2b partner path: tip ROI ~ (65, mid-35, 70, 70), partner ~
// (mid-35, 145, 70, 70) at t=0.
inline bool WriteTipAndPartner(const std::filesystem::path& p, int seconds, double tip_amp, double partner_amp) {
    auto w = Writer(p);
    if (!w.isOpened()) return false;
    const cv::Mat bg = Texture(kW, kH, 17, 160);
    for (int i = 0; i < int(kFps * seconds); ++i) {
        const double t = i / kFps;
        cv::Mat f = bg.clone();
        Disc(f, 100, int(120 + tip_amp / 2 * std::sin(2 * kPi * t)));
        Disc(f, int(200 + partner_amp / 2 * std::sin(2 * kPi * 0.7 * t)), 180);
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

// A beat-bar video (Cock Hero style): 640 x 384 at 30 fps over a busy
// picture, a dark strip near the bottom (rows 336..352, the detector's rows
// 168..176) with a bright target line at x 120 and dots that scroll left by
// 16 px per frame, one every 17 frames. vertical draws the same strip down
// the left edge of a 384 x 640 video (the detector turns it back). Returns
// the true beat times in ms (a dot on the target line); empty when the
// writer cannot open.
inline std::vector<double> WriteBeatBar(const std::filesystem::path& p, int seconds, bool vertical) {
    constexpr int W = 640, H = 384, fps = 30, y0 = 336, y1 = 352, target = 120, speed = 16, period = 17;
    const cv::Size size = vertical ? cv::Size(H, W) : cv::Size(W, H);
    cv::VideoWriter w(p.u8string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, size, true);
    if (!w.isOpened()) return {};
    const cv::Mat bg = Texture(W, H, 23, 400);
    std::vector<double> dots, beats;
    const int n = fps * seconds;
    for (int i = 0; i < n; ++i) {
        cv::Mat f = bg.clone();
        if (i % period == 0) {
            dots.push_back(W - 8);
            beats.push_back((i + double(W - 8 - target) / speed) * 1000.0 / fps);
        }
        cv::rectangle(f, cv::Rect(0, y0, W, y1 - y0), cv::Scalar(10, 10, 10), cv::FILLED);
        cv::rectangle(f, cv::Rect(target - 1, y0, 3, y1 - y0), cv::Scalar(200, 200, 200), cv::FILLED);
        std::vector<double> keep;
        for (double d : dots) {
            if (d < target) continue;
            cv::rectangle(f, cv::Rect(int(d) - 4, y0, 9, y1 - y0), cv::Scalar(250, 250, 250), cv::FILLED);
            keep.push_back(d - speed);
        }
        dots = keep;
        if (vertical) cv::rotate(f, f, cv::ROTATE_90_COUNTERCLOCKWISE);
        w.write(f);
    }
    std::vector<double> out;
    for (double b : beats)
        if (b < (n - 1) * 1000.0 / fps) out.push_back(b);
    return out;
}

}  // namespace samn::engine::testclips
