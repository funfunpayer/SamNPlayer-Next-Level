#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "samn/engine/track_box.h"
#include "samn/engine/track_cache.h"
#include "synthetic_scenes.h"
#include "track_parts.h"

// 2-D camera compensation (detection plan PR-2): the full similarity of the
// camera estimate, measured where the tip is, on both axes, gated by the
// inlier share. Synthetic scenes with known truth (GD-22); r is Pearson over
// the whole clip between the compensated track and the stroke in the world.

using namespace samn::engine;
namespace tc = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path = tc::TempPath("samn_camera");
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

constexpr int kSec = 6;
const cv::Rect kBoxY(100, 40, 120, 160), kBoxX(100, 60, 120, 120);

// Camera compensation alone: no cuts, no memory, so a test sees what the
// compensation does and nothing else.
TrackOptions Cam(TrackAxis axis, bool camera = true) {
    TrackOptions o;
    o.camera_compensation = camera;
    o.scene_cuts = false;
    o.appearance_memory = false;
    o.axis = axis;
    return o;
}

TrackResult Track(const fs::path& clip, const cv::Rect& box, const TrackOptions& o) {
    std::string error;
    auto r = TrackBox(clip, box, o, &error);
    REQUIRE_MESSAGE(r, error);
    return *r;
}

double Pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = std::min(x.size(), y.size());
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) mx += x[i], my += y[i];
    mx /= double(n);
    my /= double(n);
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
}

// r of a coordinate series against the scene's truth (a coordinate c is the
// script value -c, synthetic_scenes.h).
double R(const std::vector<double>& coord, const TrackResult& r, const tc::SceneInfo& s) {
    std::vector<double> truth, got;
    for (size_t i = 0; i < r.times_ms.size() && i < coord.size(); ++i) {
        truth.push_back(s.signal(double(r.times_ms[i]) / 1000.0));
        got.push_back(-coord[i]);
    }
    return Pearson(truth, got);
}

double R(const TrackResult& r, const tc::SceneInfo& s) { return R(r.positions, r, s); }

tc::SceneInfo Scene(const fs::path& clip, const tc::CameraScene& c) {
    const tc::SceneInfo s = tc::WriteCameraScene(clip, kSec, c);
    REQUIRE(s.ok);
    return s;
}

tc::CameraScene PanX(double amp, double hz, bool stroke_x) {
    tc::CameraScene c;
    c.stroke_x = stroke_x;
    c.camera.pan_x = tc::Sine(amp, hz);
    return c;
}

tc::CameraScene PanY(double amp, double hz, bool stroke_x) {
    tc::CameraScene c;
    c.stroke_x = stroke_x;
    c.camera.pan_y = tc::Sine(amp, hz);
    return c;
}

// A textured picture moved by a known similarity about (cx, cy).
cv::Mat Warped(const cv::Mat& src, double scale, double angle, double tx, double ty, double cx, double cy) {
    cv::Mat m = cv::getRotationMatrix2D({float(cx), float(cy)}, angle * 180.0 / tc::kPi, scale);
    m.at<double>(0, 2) += tx;
    m.at<double>(1, 2) += ty;
    cv::Mat out;
    cv::warpAffine(src, out, m, src.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

cv::Mat Gray(const cv::Mat& bgr) {
    cv::Mat g;
    cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    return g;
}

}  // namespace

TEST_CASE("camera: the estimate returns shift, scale and rotation, and where a point moves") {
    const cv::Mat a = Gray(tc::Texture(tc::kW, tc::kH, 11, 140));
    // Zoom 1.03 and 1 degree about (200, 90), then a shift of (3, -2).
    const double s = 1.03, ang = 1.0 * tc::kPi / 180.0;
    const cv::Mat b = Warped(a, s, ang, 3.0, -2.0, 200, 90);
    const CameraMotion c = EstimateCameraMotion(a, b, {});
    REQUIRE(c.ok);
    CHECK(c.Usable());
    CHECK(c.inliers > 0.8);
    CHECK(c.Scale() == doctest::Approx(s).epsilon(0.003));
    // OpenCV's positive angle turns counter-clockwise on the screen: -ang in image axes.
    CHECK(c.Angle() == doctest::Approx(-ang).epsilon(0.1));
    // The zoom centre moves only by the shift; a point 100 px left of it
    // also moves outwards, by about 3 px (so in x it nearly stays).
    const cv::Point2d at_centre = c.Shift({200, 90});
    CHECK(std::abs(at_centre.x - 3.0) < 0.2);
    CHECK(std::abs(at_centre.y + 2.0) < 0.2);
    const cv::Point2d left = c.Shift({100, 90});
    CHECK(std::abs(left.x - (3.0 - 100 * (s * std::cos(ang) - 1))) < 0.2);
    CHECK(std::abs(left.x - at_centre.x) > 2.5);
    // The old interface (the tip finder): the motion of pixel (0, 0) in y,
    // with 0.7's 3 px RANSAC distance.
    const CameraMotion c07 = EstimateCameraMotion(a, b, {}, CameraMotion::kRansacPx07);
    CHECK(CameraMotionY(a, b, {}) == c07.m(1, 2));
    CHECK(std::abs(c07.m(1, 2) - c.m(1, 2)) < 0.1);
}

TEST_CASE("camera: an identity estimate shifts nothing, and noise is not trusted") {
    CameraMotion id;
    id.ok = true;
    id.inliers = 1;
    CHECK(id.Shift({123.5, 77.25}) == cv::Point2d(0, 0));

    cv::RNG rng(5);
    cv::Mat a(tc::kH, tc::kW, CV_8U), b(tc::kH, tc::kW, CV_8U);
    rng.fill(a, cv::RNG::UNIFORM, 0, 160);
    rng.fill(b, cv::RNG::UNIFORM, 0, 160);
    const CameraMotion noise = EstimateCameraMotion(a, b, {});
    CHECK(noise.inliers < CameraMotion::kMinInliers);
    CHECK_FALSE(noise.Usable());
}

TEST_CASE("camera: pan along x at several speeds, x stroke") {
    TempDir dir;
    // 0.11 Hz (the bench's pan_x, integer offsets), then sub-pixel pans that
    // the 3 s detrend cannot remove; 0.5 Hz +-60 px is faster than the
    // stroke itself.
    struct Case {
        double amp, hz;
    };
    const std::vector<Case> cases = {{30, 0.11}, {45, 0.25}, {60, 0.5}};
    {
        const fs::path clip = dir.path / "pan_x_slow.mp4";
        const tc::SceneInfo s = tc::WritePanningX(clip, kSec, 80);
        REQUIRE(s.ok);
        const TrackResult r = Track(clip, kBoxX, Cam(TrackAxis::kX));
        CHECK_MESSAGE(R(r, s) >= 0.99, "WritePanningX r " << R(r, s));
    }
    for (const Case& k : cases) {
        const fs::path clip = dir.path / ("pan_x_" + std::to_string(k.hz) + ".mp4");
        const tc::SceneInfo s = Scene(clip, PanX(k.amp, k.hz, true));
        const TrackResult raw = Track(clip, kBoxX, Cam(TrackAxis::kX, false));
        const TrackResult comp = Track(clip, kBoxX, Cam(TrackAxis::kX));
        INFO("pan +-" << k.amp << " px at " << k.hz << " Hz: raw r " << R(raw, s) << ", compensated r " << R(comp, s));
        CHECK(R(raw, s) < 0.9);  // the scene really carries the pan
        CHECK(R(comp, s) >= 0.99);
        // The frame pixels stay what the tracker saw.
        CHECK(comp.frame_xs == raw.xs);
        CHECK(comp.frame_ys == raw.ys);
    }
}

TEST_CASE("camera: pan along y at several speeds, y stroke") {
    TempDir dir;
    for (const double hz : {0.25, 0.5}) {
        const fs::path clip = dir.path / ("pan_y_" + std::to_string(hz) + ".mp4");
        const tc::SceneInfo s = Scene(clip, PanY(hz < 0.3 ? 45 : 60, hz, false));
        const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kY));
        CHECK_MESSAGE(R(comp, s) >= 0.99, hz << " Hz: compensated r " << R(comp, s));
    }
}

TEST_CASE("camera: a diagonal pan is removed on both axes") {
    TempDir dir;
    const fs::path clip = dir.path / "diagonal.mp4";
    tc::CameraScene c = PanX(40, 0.4, true);
    c.camera.pan_y = tc::Sine(40, 0.4);
    const tc::SceneInfo s = Scene(clip, c);
    const TrackResult comp = Track(clip, kBoxX, Cam(TrackAxis::kX));
    CHECK_MESSAGE(R(comp, s) >= 0.99, "x stroke r " << R(comp, s));
    // The other axis carries no stroke and, compensated, almost no motion.
    const auto [lo, hi] = std::minmax_element(comp.ys.begin(), comp.ys.end());
    CHECK_MESSAGE(*hi - *lo < 12, "y spread " << *hi - *lo << " px (the pan alone moves 80)");
}

TEST_CASE("camera: zoom about the frame centre, ramp and oscillation") {
    TempDir dir;
    {
        const fs::path clip = dir.path / "zoom_ramp.mp4";
        const tc::SceneInfo s = tc::WriteZoomRamp(clip, kSec, 80, 1.4);
        REQUIRE(s.ok);
        const TrackResult raw = Track(clip, kBoxY, Cam(TrackAxis::kY, false));
        const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kY));
        // The tip sits at the zoom centre: the zoom must add nothing.
        CHECK_MESSAGE(R(comp, s) >= R(raw, s) - 0.005, "raw r " << R(raw, s) << ", compensated r " << R(comp, s));
        CHECK(R(comp, s) >= 0.99);
    }
    {
        const fs::path clip = dir.path / "zoom_osc.mp4";
        tc::CameraScene c;
        c.camera.zoom = [](double t) { return 1 + 0.2 * std::sin(2 * tc::kPi * 0.3 * t); };
        const tc::SceneInfo s = Scene(clip, c);
        const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kY));
        CHECK_MESSAGE(R(comp, s) >= 0.975, "zoom in/out r " << R(comp, s));
    }
}

TEST_CASE("camera: pan and zoom together, the tip off the zoom centre") {
    TempDir dir;
    const fs::path clip = dir.path / "pan_zoom.mp4";
    tc::CameraScene c = PanX(40, 0.2, false);
    c.camera.zoom = [](double t) { return 1 + 0.25 * std::sin(2 * tc::kPi * 0.15 * t); };
    const tc::SceneInfo s = Scene(clip, c);
    const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kAuto));
    CHECK_FALSE(comp.horizontal);
    // The stroke keeps the zoom's scale (the compensation removes motion,
    // it does not rescale): +-25 % amplitude caps r near 0.985 here.
    CHECK_MESSAGE(R(comp, s) >= 0.975, "pan + zoom r " << R(comp, s));
}

TEST_CASE("camera: without camera motion the compensation changes nothing that matters") {
    TempDir dir;
    const fs::path clip = dir.path / "clean.mp4";
    REQUIRE(tc::WriteMoving(clip, kSec, 80.0, 1.0));
    const TrackResult raw = Track(clip, kBoxY, Cam(TrackAxis::kAuto, false));
    const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kAuto));
    REQUIRE(comp.positions.size() == raw.positions.size());
    CHECK(comp.horizontal == raw.horizontal);
    CHECK(comp.frame_xs == raw.xs);
    CHECK(comp.frame_ys == raw.ys);
    double worst = 0;
    for (size_t i = 0; i < raw.positions.size(); ++i) {
        worst = std::max(worst, std::abs(comp.xs[i] - raw.xs[i]));
        worst = std::max(worst, std::abs(comp.ys[i] - raw.ys[i]));
    }
    // The estimate of a still camera is not exactly the identity (codec
    // noise: about 0.002 px per frame here); what it adds stays far below a pixel.
    CHECK_MESSAGE(worst < 0.05, "largest change " << worst << " px");
    CHECK(comp.stats.camera_frames_lost == 0);
}

TEST_CASE("camera: estimates without stable background are left out (inlier gate)") {
    TempDir dir;
    const fs::path clip = dir.path / "noise.mp4";
    tc::CameraScene c;
    c.noise_background = true;
    const tc::SceneInfo s = Scene(clip, c);
    const TrackResult raw = Track(clip, kBoxY, Cam(TrackAxis::kY, false));
    const TrackResult comp = Track(clip, kBoxY, Cam(TrackAxis::kY));
    // Without the gate this was a random walk: r 0.09 against 0.999 raw.
    CHECK_MESSAGE(R(comp, s) >= R(raw, s) - 0.005, "raw r " << R(raw, s) << ", compensated r " << R(comp, s));
    CHECK(comp.stats.camera_frames_lost >= comp.stats.total_frames * 9 / 10);
}

TEST_CASE("camera: a large moving foreground does not drag the compensation") {
    TempDir dir;
    const fs::path clip = dir.path / "foreground.mp4";
    tc::CameraScene c = PanX(40, 0.3, true);
    c.foreground = true;
    const tc::SceneInfo s = Scene(clip, c);
    TrackOptions o = Cam(TrackAxis::kX);
    o.record_checks = true;
    const TrackResult comp = Track(clip, kBoxX, o);
    CHECK_MESSAGE(R(comp, s) >= 0.98, "pan + foreground r " << R(comp, s));
}

TEST_CASE("camera: a hard cut during a pan starts the compensation again") {
    TempDir dir;
    const fs::path clip = dir.path / "cut_pan.mp4";
    tc::CameraScene c = PanX(45, 0.25, true);
    c.cut_frame = 75;
    const tc::SceneInfo s = Scene(clip, c);
    TrackOptions o = Cam(TrackAxis::kX);
    o.scene_cuts = true;
    o.appearance_memory = true;
    std::string error;
    // A tight box, so the memory can find the disc after the cut (track_box_test).
    const auto r = TrackBox(clip, cv::Rect(125, 85, 70, 70), o, &error);
    REQUIRE_MESSAGE(r, error);
    REQUIRE(r->scene_cuts == std::vector<int>{75});
    // Each scene measured on its own: the second starts where the disc is
    // at the cut, so no accumulated pan of the first carries over.
    CHECK_MESSAGE(R(*r, s) >= 0.97, "r over the cut " << R(*r, s));
    // Across the cut the compensated track moves as the stroke does (the
    // first scene's pan, 45 px at the cut, is not carried over).
    const auto truth = [&](size_t i) { return -s.signal(double(r->times_ms[i]) / 1000.0); };
    const double step = (r->xs[76] - r->xs[74]) - (truth(76) - truth(74));
    CHECK_MESSAGE(std::abs(step) < 4, "step at the cut " << step << " px beyond the stroke");
}

TEST_CASE("camera: the axis is chosen on the compensated motion, not the pan") {
    TempDir dir;
    {
        // A y stroke (80 px) under an x pan of +-80 px: raw x would win.
        const fs::path clip = dir.path / "y_stroke_x_pan.mp4";
        const tc::SceneInfo s = Scene(clip, PanX(80, 0.25, false));
        const TrackResult r = Track(clip, kBoxY, Cam(TrackAxis::kAuto));
        CHECK_FALSE(r.horizontal);
        CHECK(R(r, s) >= 0.99);
    }
    {
        // An x stroke under a y pan of +-80 px.
        const fs::path clip = dir.path / "x_stroke_y_pan.mp4";
        const tc::SceneInfo s = Scene(clip, PanY(80, 0.25, true));
        const TrackResult r = Track(clip, kBoxX, Cam(TrackAxis::kAuto));
        CHECK(r.horizontal);
        CHECK(R(r, s) >= 0.99);
    }
}

TEST_CASE("camera: the track cache keeps the frame pixels of both axes") {
    TempDir dir;
    TrackResult r;
    r.times_ms = {0, 40, 80};
    r.positions = {1, 2, 3};
    r.lost = {0, 0, 0};
    r.xs = {1, 2, 3};
    r.ys = {4, 5, 6};
    r.frame_xs = {11, 12, 13};
    r.frame_ys = {14, 15, 16};
    SaveTrackCache(dir.path, "k", r);
    const auto back = LoadTrackCache(dir.path, "k");
    REQUIRE(back);
    CHECK(back->xs == r.xs);
    CHECK(back->frame_xs == r.frame_xs);
    CHECK(back->frame_ys == r.frame_ys);
}
