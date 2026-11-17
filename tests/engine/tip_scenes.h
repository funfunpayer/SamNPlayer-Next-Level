#pragma once

// POV-like synthetic scenes for the automatic tip box (tip finder, region
// box, tip tracking): a large, softly shaded mover close to the camera
// strokes along any direction over a textured world, while the camera may
// follow the action, shake, or drift, and a camera-fixed occluder may hide
// part of the stroke. Every writer is deterministic. Synthetic shapes only:
// no real media, ever.
//
// Ground truth: TipSceneInfo::centre is the mover's centre in frame pixels
// for every frame (also while it is partly hidden), stroke(t) is its motion
// in the scene along the stroke direction (px; the camera is not part of it).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "synthetic_clips.h"

namespace samn::engine::testclips {

struct TipScene {
    int seconds = 12;
    double amp = 70, hz = 1.0;    // the stroke, peak to peak (px) and rate
    double angle_deg = 90;        // stroke direction in the picture: 90 = y, 0 = x, 45 = diagonal
    int mover_w = 150, mover_h = 130;  // the mover's full axes (an ellipse)
    double rest_x = 160, rest_y = 120; // its centre at rest (frame px)
    // The camera follows this share of the stroke, follow_lag_s late (a
    // hand-held POV camera moves with the action): in the frame the mover
    // moves less and the background moves against it.
    double follow = 0, follow_lag_s = 0.12;
    double shake_px = 0;          // hand shake (4-9 Hz) on both axes
    double drift_px = 0;          // slow camera drift (0.07 / 0.05 Hz)
    // A camera-fixed, textured dark band across the bottom of the frame from
    // this y down (0 = none): the mover dips under it (partial occlusion).
    int occluder_y = 0;
    // The mover stands still for the first still_s seconds (an intro).
    double still_s = 0;
    // The mover grows and shrinks with the stroke by this share (it comes
    // towards the camera at one end of the stroke).
    double pulse = 0;
    // The world behind the mover is tinted towards the mover's own colour by
    // this share (0..1): skin on skin, little contrast.
    double camouflage = 0;
    // A camera-fixed dark card hides the whole mover in [cover_from_s,
    // cover_to_s) (a hand in front of the lens).
    double cover_from_s = -1, cover_to_s = -1;
    // A small second mover (radius 16) at the top-left that strokes
    // vertically at 1.7 Hz with distractor_amp (0 = none).
    double distractor_amp = 0;
};

struct TipSceneInfo {
    bool ok = false;
    double fps = kFps;
    int frames = 0;
    int w = kW, h = kH;
    int mover_w = 0, mover_h = 0;
    double dir_x = 0, dir_y = 1;              // the stroke direction (unit)
    std::vector<cv::Point2d> centre;          // the mover's centre per frame (frame px)
    std::function<double(double)> stroke;     // t (s) -> the mover's offset along the direction, in the scene
    double cover_from_s = -1, cover_to_s = -1; // the whole picture is covered in [from, to)

    // The mover's box in frame f (frame px, not clipped).
    cv::Rect MoverBox(int f) const {
        const cv::Point2d c = centre[size_t(std::clamp(f, 0, frames - 1))];
        return {int(std::lround(c.x - mover_w / 2.0)), int(std::lround(c.y - mover_h / 2.0)), mover_w, mover_h};
    }
    // Everything the mover covers from frame a to frame b (exclusive).
    cv::Rect Swept(int a, int b) const {
        cv::Rect r = MoverBox(a);
        for (int f = a; f < b && f < frames; ++f) r |= MoverBox(f);
        return r;
    }
};

namespace tipscene {

// The mover: an ellipse with soft shading and a few blobs that move with it
// (skin-like: large, low in contrast, not a flat disc).
inline void MakeMover(int w, int h, cv::Mat* bgr, cv::Mat* alpha) {
    *bgr = cv::Mat(h, w, CV_8UC3, cv::Scalar(0, 0, 0));
    *alpha = cv::Mat::zeros(h, w, CV_32F);
    cv::Mat mask = cv::Mat::zeros(h, w, CV_8U);
    cv::ellipse(mask, {w / 2, h / 2}, {w / 2 - 2, h / 2 - 2}, 0, 0, 360, cv::Scalar(255), cv::FILLED);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double nx = (x - w / 2.0) / (w / 2.0), ny = (y - h / 2.0) / (h / 2.0);
            const double shade = 1.0 - 0.35 * (nx * nx + ny * ny) + 0.12 * nx;
            bgr->at<cv::Vec3b>(y, x) = cv::Vec3b(cv::saturate_cast<uchar>(150 * shade), cv::saturate_cast<uchar>(170 * shade),
                                                 cv::saturate_cast<uchar>(215 * shade));
        }
    std::mt19937 rng(41);
    auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    for (int i = 0; i < 14; ++i) {
        const int x = w / 6 + pick(2 * w / 3), y = h / 6 + pick(2 * h / 3), r = 5 + pick(8);
        const int d = pick(2) ? 38 : -38;
        cv::circle(*bgr, {x, y}, r, cv::Scalar(150 + d, 165 + d, 205 + d / 2), cv::FILLED, cv::LINE_AA);
    }
    cv::GaussianBlur(*bgr, *bgr, {5, 5}, 0);
    mask.convertTo(*alpha, CV_32F, 1.0 / 255);
    cv::GaussianBlur(*alpha, *alpha, {5, 5}, 0);
}

// Draws sprite (with alpha) centred on (cx, cy), sub-pixel.
inline void Blend(cv::Mat& frame, const cv::Mat& sprite, const cv::Mat& alpha, double cx, double cy) {
    const cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, cx - sprite.cols / 2.0, 0, 1, cy - sprite.rows / 2.0);
    cv::Mat s, a;
    cv::warpAffine(sprite, s, m, frame.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    cv::warpAffine(alpha, a, m, frame.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    for (int y = 0; y < frame.rows; ++y) {
        cv::Vec3b* f = frame.ptr<cv::Vec3b>(y);
        const cv::Vec3b* sp = s.ptr<cv::Vec3b>(y);
        const float* ap = a.ptr<float>(y);
        for (int x = 0; x < frame.cols; ++x) {
            const float k = ap[x];
            if (k <= 0) continue;
            for (int c = 0; c < 3; ++c) f[x][c] = cv::saturate_cast<uchar>(f[x][c] * (1 - k) + sp[x][c] * k);
        }
    }
}

inline double ShakeAt(double t, double amp, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> phase(0, 2 * kPi);
    const double p1 = phase(rng), p2 = phase(rng), p3 = phase(rng);
    return amp * (0.5 * std::sin(2 * kPi * 4.3 * t + p1) + 0.3 * std::sin(2 * kPi * 6.1 * t + p2) +
                  0.2 * std::sin(2 * kPi * 8.7 * t + p3));
}

}  // namespace tipscene

inline TipSceneInfo WriteTipScene(const std::filesystem::path& p, const TipScene& c) {
    TipSceneInfo s;
    s.mover_w = c.mover_w;
    s.mover_h = c.mover_h;
    s.cover_from_s = c.cover_from_s;
    s.cover_to_s = c.cover_to_s;
    s.dir_x = std::cos(c.angle_deg * kPi / 180);
    s.dir_y = std::sin(c.angle_deg * kPi / 180);
    auto w = Writer(p);
    if (!w.isOpened()) return s;
    constexpr int pad = 120;
    cv::Mat world = Texture(kW + 2 * pad, kH + 2 * pad, 11, int(1.65 * (kW + 2 * pad) * (kH + 2 * pad) / 1000.0));
    if (c.camouflage > 0) {
        const cv::Mat skin(world.size(), CV_8UC3, cv::Scalar(140, 158, 200));
        cv::addWeighted(world, 1 - c.camouflage, skin, c.camouflage, 0, world);
    }
    const cv::Mat cover = Texture(kW, kH, 83, 30);
    cv::Mat mover, alpha;
    tipscene::MakeMover(c.mover_w, c.mover_h, &mover, &alpha);
    cv::Mat occluder;
    if (c.occluder_y > 0) {
        occluder = Texture(kW, kH - c.occluder_y, 67, 40);
        occluder.convertTo(occluder, -1, 0.45, 5);
    }
    const double amp = c.amp, hz = c.hz, still = c.still_s;
    s.stroke = [amp, hz, still](double t) {
        return t < still ? 0.0 : amp / 2 * std::sin(2 * kPi * hz * (t - still));
    };
    s.frames = int(kFps * c.seconds);
    for (int i = 0; i < s.frames; ++i) {
        const double t = i / kFps;
        const double d = s.stroke(t);
        const double lagged = s.stroke(std::max(0.0, t - c.follow_lag_s));
        double cam_x = c.follow * lagged * s.dir_x, cam_y = c.follow * lagged * s.dir_y;
        if (c.shake_px > 0) {
            cam_x += tipscene::ShakeAt(t, c.shake_px, 7);
            cam_y += tipscene::ShakeAt(t, c.shake_px * 0.7, 8);
        }
        if (c.drift_px > 0) {
            cam_x += c.drift_px * std::sin(2 * kPi * 0.07 * t);
            cam_y += c.drift_px * 0.6 * std::sin(2 * kPi * 0.05 * t);
        }
        const cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, -(pad + cam_x), 0, 1, -(pad + cam_y));
        cv::Mat f;
        cv::warpAffine(world, f, m, {kW, kH}, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        if (c.distractor_amp > 0)
            cv::circle(f,
                       {int(std::lround(34 - cam_x)),
                        int(std::lround(44 + c.distractor_amp / 2 * std::sin(2 * kPi * 1.7 * t) - cam_y))},
                       16, cv::Scalar(240, 240, 240), cv::FILLED, cv::LINE_AA);
        const cv::Point2d centre(c.rest_x + d * s.dir_x - cam_x, c.rest_y + d * s.dir_y - cam_y);
        if (c.pulse > 0) {
            const double k = 1 + c.pulse * (c.amp > 0 ? d / (c.amp / 2) : 0);
            cv::Mat m2, a2;
            cv::resize(mover, m2, cv::Size(), k, k, cv::INTER_LINEAR);
            cv::resize(alpha, a2, cv::Size(), k, k, cv::INTER_LINEAR);
            tipscene::Blend(f, m2, a2, centre.x, centre.y);
        } else {
            tipscene::Blend(f, mover, alpha, centre.x, centre.y);
        }
        if (t >= c.cover_from_s && t < c.cover_to_s) {
            cv::Mat dark;
            cover.convertTo(dark, -1, 0.5, 0);
            dark.copyTo(f);
        }
        if (!occluder.empty()) occluder.copyTo(f(cv::Rect(0, c.occluder_y, kW, kH - c.occluder_y)));
        s.centre.push_back(centre);
        w.write(f);
    }
    s.ok = true;
    return s;
}

// The named scenes of the tip benchmark (engine/tools/samn_tip_bench) and
// the tip tests. Picked once, not tuned to the finder.
struct NamedTipScene {
    std::string name, what;
    TipScene scene;
};

inline std::vector<NamedTipScene> TipScenes() {
    std::vector<NamedTipScene> v;
    auto add = [&](const char* name, const char* what, const std::function<void(TipScene&)>& set) {
        TipScene c;
        set(c);
        v.push_back({name, what, c});
    };
    add("pov_y", "large mover, Y stroke, still camera", [](TipScene&) {});
    add("pov_x", "large mover, X stroke, still camera", [](TipScene& c) { c.angle_deg = 0; });
    add("pov_diag", "large mover, diagonal stroke (45 deg), still camera", [](TipScene& c) { c.angle_deg = 45; });
    add("pov_follow_y", "the camera follows 50 % of a Y stroke, 120 ms late", [](TipScene& c) { c.follow = 0.5; });
    add("pov_follow_x", "the camera follows 50 % of an X stroke, 120 ms late", [](TipScene& c) {
        c.angle_deg = 0;
        c.follow = 0.5;
    });
    add("pov_shake", "hand shake +-5 px and drift +-25 px, Y stroke", [](TipScene& c) {
        c.shake_px = 5;
        c.drift_px = 25;
    });
    add("pov_occluded", "the lower third of the Y stroke dips under a camera-fixed band", [](TipScene& c) {
        c.occluder_y = 170;
        c.rest_y = 125;
    });
    add("pov_near", "a very large mover (230 x 190), diagonal 60 deg, the camera follows 30 %", [](TipScene& c) {
        c.mover_w = 230;
        c.mover_h = 190;
        c.angle_deg = 60;
        c.follow = 0.3;
        c.amp = 60;
    });
    add("pov_distractor", "X stroke with a small fast vertical mover in the corner", [](TipScene& c) {
        c.angle_deg = 0;
        c.distractor_amp = 40;
    });
    add("pov_fast", "a 2.5 Hz Y stroke of 90 px (up to 47 px per frame at 15 fps)", [](TipScene& c) {
        c.hz = 2.5;
        c.amp = 90;
    });
    add("pov_pulse", "the mover grows and shrinks by 20 % with a diagonal stroke (towards the camera)",
        [](TipScene& c) {
            c.angle_deg = 70;
            c.pulse = 0.2;
        });
    add("pov_camouflage", "little contrast: the world is tinted 70 % towards the mover's colour, X stroke",
        [](TipScene& c) {
            c.angle_deg = 0;
            c.camouflage = 0.7;
        });
    add("pov_cover", "the whole picture is covered from 5.0 to 6.5 s, Y stroke", [](TipScene& c) {
        c.cover_from_s = 5.0;
        c.cover_to_s = 6.5;
    });
    add("pov_edge", "the stroke takes the mover half out of the picture at the bottom", [](TipScene& c) {
        c.rest_y = 185;
        c.amp = 90;
    });
    return v;
}

}  // namespace samn::engine::testclips
