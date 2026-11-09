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

// A camera over a large textured world: pan_x / pan_y move the camera centre
// (world px, 0 at the start), zoom scales about the frame centre (1 = the
// start). The world point under the camera centre is shown at the frame
// centre: frame = zoom * (world - centre(t)) + (w/2, h/2), sub-pixel.
struct CameraPath {
    std::function<double(double)> pan_x = [](double) { return 0.0; };
    std::function<double(double)> pan_y = [](double) { return 0.0; };
    std::function<double(double)> zoom = [](double) { return 1.0; };
};

inline std::function<double(double)> Sine(double amp, double hz) {
    return [amp, hz](double t) { return amp * std::sin(2 * kPi * hz * t); };
}

struct CameraScene {
    double amp = 80, hz = 1.0;  // the tip disc's stroke in the world
    bool stroke_x = false;      // stroke along x (else y)
    CameraPath camera;
    // A hard cut to another, darker world at this frame (-1: none). The
    // camera keeps its path; the disc starts under the camera centre again.
    int cut_frame = -1;
    // A large textured disc (radius 95) in front of the camera (frame
    // coordinates, not the world) right of the tip, swinging along y at
    // 0.35 Hz, the tip drawn over it: its features move against the
    // background and compete in the camera estimate.
    bool foreground = false;
    // Background boxes per 1000 px^2 of world (the other scenes: about 1.65).
    double texture_density = 1.65;
    // A fresh noise picture behind the disc in every frame: no stable
    // feature, so a camera estimate is a random walk (GD-22, REJ-09).
    bool noise_background = false;
};

// The tip disc (radius 35) sits at the frame centre at t = 0 (and after a
// cut). Truth: the stroke in the world; the camera is not part of it.
inline SceneInfo WriteCameraScene(const std::filesystem::path& p, int seconds, const CameraScene& c) {
    SceneInfo s;
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    constexpr int pad = 220;
    const int ww = kW + 2 * pad, wh = kH + 2 * pad;
    const int boxes = int(c.texture_density * ww * wh / 1000.0);
    const cv::Mat world1 = Texture(ww, wh, 11, boxes);
    cv::Mat world2 = Texture(ww, wh, 99, boxes);
    world2.convertTo(world2, -1, 0.4, 12);
    const cv::Mat fg_texture = Texture(kW, kH, 31, 400);
    cv::RNG rng(77);
    s.frames = int(kFps * seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        const bool after_cut = c.cut_frame >= 0 && i >= c.cut_frame;
        const double t0 = after_cut ? c.cut_frame / kFps : 0.0;
        const double ax = ww / 2.0 + c.camera.pan_x(t0), ay = wh / 2.0 + c.camera.pan_y(t0);
        const double d = Stroke(t, c.amp, c.hz);
        cv::Mat world = (after_cut ? world2 : world1).clone();
        if (c.noise_background) rng.fill(world, cv::RNG::UNIFORM, 0, 160);
        Disc(world, int(std::lround(ax + (c.stroke_x ? d : 0))), int(std::lround(ay + (c.stroke_x ? 0 : d))));
        const double z = c.camera.zoom(t);
        const double cx = ww / 2.0 + c.camera.pan_x(t), cy = wh / 2.0 + c.camera.pan_y(t);
        const cv::Mat m = (cv::Mat_<double>(2, 3) << z, 0, kW / 2.0 - z * cx, 0, z, kH / 2.0 - z * cy);
        cv::Mat f;
        cv::warpAffine(world, f, m, {kW, kH}, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        if (c.foreground) {
            const int fy = int(std::lround(50 * std::sin(2 * kPi * 0.35 * t)));
            cv::Mat mask = cv::Mat::zeros(kH, kW, CV_8U), moved;
            cv::circle(mask, {275, 120 + fy}, 95, cv::Scalar(255), cv::FILLED);
            const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1, 0, 0, 0, 1, fy);
            cv::warpAffine(fg_texture, moved, shift, {kW, kH}, cv::INTER_NEAREST, cv::BORDER_WRAP);
            moved.copyTo(f, mask);
            // The tip stays in front: this is about the camera estimate, not occlusion.
            const double tx = ax + (c.stroke_x ? d : 0), ty = ay + (c.stroke_x ? 0 : d);
            DiscR(f, int(std::lround(z * (tx - cx) + kW / 2.0)), int(std::lround(z * (ty - cy) + kH / 2.0)),
                  int(std::lround(35 * z)));
        }
        w.write(f);
    }
    const double amp = c.amp, hz = c.hz;
    s.signal = [amp, hz](double t) { return -Stroke(t, amp, hz); };
    if (c.cut_frame >= 0) s.cut_frames = {c.cut_frame};
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
