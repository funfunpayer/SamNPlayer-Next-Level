#pragma once

// Synthetic scenes with known ground truth for the detection benchmark
// (tools/samn_bench) and for tests. Built on synthetic_clips.h: the same
// textured background and bright disc, so the numbers stay comparable with
// the spec tests. Every writer is deterministic (fixed seeds). Synthetic
// shapes only: no real media, ever.
//
// Ground truth: SceneInfo::signal(t) is the script value the scene stands
// for, up = high. Image y grows downwards and a script peak is the disc at
// its top, so a disc coordinate c gives signal -c (also for an x stroke: the
// left-most position is the peak, spec T3). It is the disc's motion in the
// scene, not in the frame: camera pan and zoom are not part of it.

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "synthetic_clips.h"

namespace samn::engine::testclips {

struct SceneInfo {
    bool ok = false;
    double fps = kFps;
    int frames = 0;
    int w = kW, h = kH;
    std::function<double(double)> signal;                  // t in seconds -> script value (px, up = high)
    std::vector<std::pair<double, double>> hidden_s;       // [from, to) seconds with no disc to see
    std::vector<int> cut_frames;                           // true scene cuts (frame numbers)

    double seconds() const { return frames / fps; }
    bool Hidden(double t) const {
        for (const auto& h_ : hidden_s)
            if (t >= h_.first && t < h_.second) return true;
        return false;
    }
};

inline double Stroke(double t, double amp, double hz, double phase = 0) {
    return amp / 2 * std::sin(2 * kPi * hz * t + phase);
}

inline cv::VideoWriter WriterFor(const std::filesystem::path& p, int w, int h, double fps) {
    return cv::VideoWriter(p.u8string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, {w, h}, true);
}

inline void DiscR(cv::Mat& f, int cx, int cy, int r) {
    cv::circle(f, {cx, cy}, r, cv::Scalar(250, 250, 250), cv::FILLED);
}

// The same camera path as WritePanning.
inline void PanOffsets(double t, int* ox, int* oy) {
    *ox = int(60 + 30 * std::sin(2 * kPi * t * 0.11));
    *oy = int(60 + 20 * std::sin(2 * kPi * t * 0.08));
}

// Camera pan as WritePanning, but the disc strokes along x (world x 220 +- amp/2).
inline SceneInfo WritePanningX(const std::filesystem::path& p, int seconds, double amp) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const int pad = 60;
    const cv::Mat world = Texture(kW + 2 * pad, kH + 2 * pad, 11, 260);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        int ox, oy;
        PanOffsets(t, &ox, &oy);
        cv::Mat f = world(cv::Rect(ox, oy, kW, kH)).clone();
        Disc(f, int(60 + 160 + Stroke(t, amp, 1.0)) - ox, 60 + 120 - oy);
        w.write(f);
    }
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// The camera zooms in from 1 to zoom_end about the centre while the disc strokes 1 Hz.
inline SceneInfo WriteZoomRamp(const std::filesystem::path& p, int seconds, double amp, double zoom_end) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        const double z = 1 + (zoom_end - 1) * t / seconds;
        cv::Mat world = bg.clone();
        Disc(world, 160, int(120 + Stroke(t, amp, 1.0)));
        const cv::Mat m = (cv::Mat_<double>(2, 3) << z, 0, (1 - z) * kW / 2, 0, z, (1 - z) * kH / 2);
        cv::Mat f;
        cv::warpAffine(world, f, m, {kW, kH}, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        w.write(f);
    }
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// A 1 Hz disc that is not there between from_s and to_s (as spec T6).
inline SceneInfo WriteOcclusion(const std::filesystem::path& p, int seconds, double amp, double from_s, double to_s) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        cv::Mat f = bg.clone();
        if (t < from_s || t >= to_s) Disc(f, 160, int(120 + Stroke(t, amp, 1.0)));
        w.write(f);
    }
    s.hidden_s.push_back({from_s, to_s});
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// A fast disc smeared over a full frame interval (8 sub-frames averaged).
inline SceneInfo WriteMotionBlur(const std::filesystem::path& p, int seconds, double amp, double hz) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    constexpr int kSub = 8;
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        cv::Mat acc = cv::Mat::zeros(kH, kW, CV_32FC3);
        for (int k = 0; k < kSub; ++k) {
            const double t = (i + (k + 0.5) / kSub - 0.5) / kFps;
            cv::Mat f = bg.clone();
            Disc(f, 160, int(120 + Stroke(t, amp, hz)));
            cv::accumulate(f, acc);
        }
        cv::Mat out;
        acc.convertTo(out, CV_8UC3, 1.0 / kSub);
        w.write(out);
    }
    s.signal = [amp, hz](double t) { return -Stroke(t, amp, hz); };
    s.ok = true;
    return s;
}

// Gaussian noise of sigma on every frame, then a JPEG round trip at a low
// quality (blocking, ringing), then the video encoder: heavy compression.
inline SceneInfo WriteNoisyCompressed(const std::filesystem::path& p, int seconds, double amp, double sigma,
                                      int jpeg_quality) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    cv::RNG rng(1234);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        cv::Mat f = bg.clone();
        Disc(f, 160, int(120 + Stroke(t, amp, 1.0)));
        cv::Mat noise(kH, kW, CV_32FC3), fl;
        rng.fill(noise, cv::RNG::NORMAL, 0.0, sigma);
        f.convertTo(fl, CV_32FC3);
        fl += noise;
        fl.convertTo(f, CV_8UC3);
        std::vector<uchar> buf;
        cv::imencode(".jpg", f, buf, {cv::IMWRITE_JPEG_QUALITY, jpeg_quality});
        w.write(cv::imdecode(buf, cv::IMREAD_COLOR));
    }
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// Dropped frames the way a re-encode shows them: a share of the frames is a
// repeat of the one before while the disc keeps moving. (The container
// cannot be made variable-rate with OpenCV's writer.)
inline SceneInfo WriteFrozenFrames(const std::filesystem::path& p, int seconds, double amp, double drop_share,
                                   unsigned seed) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    std::mt19937 rng(seed);
    cv::Mat last;
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        const bool drop = i > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < drop_share;
        if (!drop) {
            last = bg.clone();
            Disc(last, 160, int(120 + Stroke(t, amp, 1.0)));
        }
        w.write(last);
    }
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// The tip disc (x 110) strokes 1 Hz; a second, identical disc (x 210) strokes
// at the same rate with distractor_phase radians. Pi = anti-phase, pi/2 =
// quarter phase. Tip box: (55, 40, 110, 160).
inline SceneInfo WriteDistractor(const std::filesystem::path& p, int seconds, double amp, double distractor_phase) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        cv::Mat f = bg.clone();
        Disc(f, 110, int(120 + Stroke(t, amp, 1.0)));
        Disc(f, 210, int(120 + Stroke(t, amp, 1.0, distractor_phase)));
        w.write(f);
    }
    s.signal = [amp](double t) { return -Stroke(t, amp, 1.0); };
    s.ok = true;
    return s;
}

// A small, fast clip, 256 x 144 at 30 fps, no cut: a radius 18 disc strokes
// 2.5 Hz. Box: (78, 10, 100, 124).
inline SceneInfo WriteFastSmall(const std::filesystem::path& p, int seconds, double amp, double hz) {
    SceneInfo s;
    s.w = 256;
    s.h = 144;
    s.fps = 30;
    auto w = WriterFor(p, s.w, s.h, s.fps);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(s.w, s.h, 5, 90);
    s.frames = int(s.fps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        cv::Mat f = bg.clone();
        DiscR(f, 128, int(72 + Stroke(i / s.fps, amp, hz)), 18);
        w.write(f);
    }
    s.signal = [amp, hz](double t) { return -Stroke(t, amp, hz); };
    s.ok = true;
    return s;
}

// A long clip whose stroke centre drifts slowly (period drift_period_s, +-
// drift_amp px) while the disc strokes 1 Hz: detrend and long-run timing.
inline SceneInfo WriteDrift(const std::filesystem::path& p, int seconds, double amp, double drift_amp,
                            double drift_period_s) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    const cv::Mat bg = Texture(kW, kH, 11, 140);
    const auto coord = [=](double t) { return drift_amp * std::sin(2 * kPi * t / drift_period_s) + Stroke(t, amp, 1.0); };
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        cv::Mat f = bg.clone();
        Disc(f, 160, int(120 + coord(i / kFps)));
        w.write(f);
    }
    s.signal = [coord](double t) { return -coord(t); };
    s.ok = true;
    return s;
}

}  // namespace samn::engine::testclips
