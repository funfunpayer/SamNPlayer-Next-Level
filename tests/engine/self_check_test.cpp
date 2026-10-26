#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "samn/engine/self_check.h"

using namespace samn::engine;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

std::vector<double> Times(int n, double fps = 30) {
    std::vector<double> t(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) t[static_cast<size_t>(i)] = i * 1000.0 / fps;
    return t;
}

// Image y of a box stroking at 1.4 Hz with 30 px amplitude, 30 fps.
std::vector<double> Stroke(int n, double amp = 30) {
    std::vector<double> y(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) y[static_cast<size_t>(i)] = 150 - amp * std::sin(2 * kPi * 1.4 * i / 30.0);
    return y;
}

// What an honest flow-in-box checker sees for a track: its velocity plus noise.
std::vector<double> FlowOf(const std::vector<double>& y, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0, 0.3);
    std::vector<double> f(y.size(), 0.0);
    for (size_t i = 1; i + 1 < y.size(); ++i) f[i] = (y[i + 1] - y[i - 1]) / 2 + noise(rng);
    return f;
}

FrameChecks Healthy(const std::vector<double>& y) {
    FrameChecks c;
    c.box_flow = FlowOf(y);
    c.foreground.assign(y.size(), 0.5);
    c.tracker_ok.assign(y.size(), 1);
    c.camera_inliers.assign(y.size(), 0.9);
    return c;
}

int Count(const std::vector<WindowVerdict>& v, WindowState s) {
    int n = 0;
    for (const auto& w : v) n += w.state == s ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("Hampel rejects a spike and keeps a clean stroke") {
    auto y = Stroke(300);
    const auto clean = HampelMask(y, 3, 5, 0.5);
    CHECK(std::count(clean.begin(), clean.end(), 1) == 0);
    y[100] += 80;  // a tracker jump of one frame
    const auto bad = HampelMask(y, 3, 5, 0.5);
    CHECK(bad[100] == 1);
    CHECK(std::count(bad.begin(), bad.end(), 1) == 1);
}

TEST_CASE("Hampel factor is pinned from both sides") {
    // A 12 px jump on a line with +-1.5 px of noise (MAD 1): an outlier at
    // factor 5 (limit 9.9 px), not at factor 20 (limit 39.7 px).
    const double noise[4] = {-1.5, -0.5, 0.5, 1.5};
    std::vector<double> y(50);
    for (size_t i = 0; i < y.size(); ++i) y[i] = 100 + noise[(i * 3) % 4];
    y[25] += 12;
    CHECK(HampelMask(y, 3, 5, 0.5)[25] == 1);
    CHECK(HampelMask(y, 3, 20, 0.5)[25] == 0);
}

TEST_CASE("Gate: outlier replaced, short gap interpolated, long gap splits") {
    const int n = 300;
    const auto t = Times(n);
    auto y = Stroke(n);
    y[40] += 90;                                         // outlier
    for (int i = 100; i < 109; ++i) y[static_cast<size_t>(i)] = kNaN;  // 9 frames lost: 333 ms gap
    for (int i = 200; i < 230; ++i) y[static_cast<size_t>(i)] = kNaN;  // 30 frames: 1 s
    const auto g = GateSamples(t, y, SampleGateOptions{});
    CHECK(g.outliers == 1);
    CHECK(g.valid[40] == 1);                                       // inside a short gap after rejection
    CHECK(std::fabs(g.positions[40] - Stroke(n)[40]) < 3.0);       // interpolated, not the jump
    CHECK(g.valid[104] == 1);
    CHECK(g.valid[215] == 0);                                      // long gap stays a gap
    REQUIRE(g.segments.size() == 2);
    CHECK(g.segments[0] == std::make_pair(0, 200));
    CHECK(g.segments[1] == std::make_pair(230, n));
    CHECK(g.dropped == 30);
}

TEST_CASE("Gate: the 500 ms limit holds from both sides") {
    const int n = 200;
    const auto t = Times(n);  // 33.3 ms per frame
    auto y = Stroke(n);
    // gap from frame 50 to 63: neighbours 49 and 64 are 500 ms apart -> interpolated
    for (int i = 50; i < 64; ++i) y[static_cast<size_t>(i)] = kNaN;
    // gap from 120 to 134: neighbours 119 and 135 are 533 ms apart -> split
    for (int i = 120; i < 135; ++i) y[static_cast<size_t>(i)] = kNaN;
    const auto g = GateSamples(t, y, SampleGateOptions{});
    CHECK(g.valid[56] == 1);
    CHECK(g.valid[127] == 0);
}

TEST_CASE("Gate: edges are never extrapolated, short islands are dropped") {
    const int n = 120;
    const auto t = Times(n);
    auto y = Stroke(n);
    for (int i = 0; i < 5; ++i) y[static_cast<size_t>(i)] = kNaN;
    for (int i = 20; i < 90; ++i) y[static_cast<size_t>(i)] = kNaN;
    y[55] = 150;  // a lone sample inside a long gap: 1.2 s of nothing on each side
    const auto g = GateSamples(t, y, SampleGateOptions{});
    CHECK(g.valid[2] == 0);
    CHECK(g.valid[55] == 0);
    REQUIRE(g.segments.size() == 2);
}

TEST_CASE("Verifier: a healthy track passes every window") {
    const auto y = Stroke(900);
    const auto v = VerifyWindows(y, Healthy(y), VerifyOptions{});
    REQUIRE(v.size() == 15);
    CHECK(Count(v, WindowState::kPass) == 15);
    CHECK(v[3].c1 > 0.9);
    CHECK(v[3].c2 == doctest::Approx(1.0));
}

TEST_CASE("Verifier: a lost tracker fails its windows with the reason") {
    auto y = Stroke(900);
    auto c = Healthy(y);
    for (int i = 300; i < 420; ++i) {
        y[static_cast<size_t>(i)] = kNaN;
        c.tracker_ok[static_cast<size_t>(i)] = 0;
    }
    const auto v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(v[5].state == WindowState::kFail);
    CHECK(v[6].state == WindowState::kFail);
    CHECK(v[5].reasons.find("c4") != std::string::npos);
    CHECK(Count(v, WindowState::kPass) == 13);
}

TEST_CASE("Verifier: a box on the wrong target fails although the tracker is happy (GD-11)") {
    // The tracker keeps a box and a stroke-like path (it latched on something
    // else that moves), but the flow inside the box and the foreground there
    // do not agree: the independent checkers catch it, the tracker does not.
    auto y = Stroke(900);
    auto c = Healthy(y);
    std::mt19937 rng(5);
    std::normal_distribution<double> noise(0, 1.0);
    for (int i = 420; i < 900; ++i) {
        c.box_flow[static_cast<size_t>(i)] = noise(rng);
        c.foreground[static_cast<size_t>(i)] = 0.02;
    }
    const auto v = VerifyWindows(y, c, VerifyOptions{});
    for (int k = 7; k < 15; ++k) {
        CHECK(v[static_cast<size_t>(k)].state == WindowState::kFail);
        CHECK(v[static_cast<size_t>(k)].c4 == doctest::Approx(1.0));
    }
    CHECK(Count(v, WindowState::kPass) == 7);
}

TEST_CASE("Verifier: a pause is still only when something independent confirms it") {
    auto y = Stroke(900);
    for (int i = 300; i < 420; ++i) y[static_cast<size_t>(i)] = 150.0 + 0.2 * ((i % 2) ? 1 : -1);
    auto c = Healthy(y);
    for (int i = 300; i < 420; ++i) c.foreground[static_cast<size_t>(i)] = 0.0;

    // No scene signal and no appearance: a pause cannot be told from a box
    // that sits on still background. Fail, not still.
    auto v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(v[5].state == WindowState::kFail);

    // The fast lane says the whole scene is quiet: still.
    c.scene_motion.assign(y.size(), 0.10);
    for (int i = 300; i < 420; ++i) c.scene_motion[static_cast<size_t>(i)] = 0.005;
    v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(v[5].state == WindowState::kStill);
    CHECK(v[6].state == WindowState::kStill);
    CHECK(v[4].state == WindowState::kPass);

    // Something else still moves in the scene: the box may have lost it. Fail.
    for (int i = 300; i < 420; ++i) c.scene_motion[static_cast<size_t>(i)] = 0.10;
    v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(v[5].state == WindowState::kFail);
}

TEST_CASE("Verifier: an untrusted camera makes flow agreement unknown, not failed") {
    const auto y = Stroke(900);
    auto c = Healthy(y);
    std::mt19937 rng(9);
    std::normal_distribution<double> noise(0, 0.5);
    for (auto& f : c.box_flow) f = noise(rng);  // flow says the box does not move with the track
    for (int i = 0; i < 300; ++i) c.camera_inliers[static_cast<size_t>(i)] = 0.1;
    const auto v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(std::isnan(v[0].c1));
    CHECK(v[0].state == WindowState::kPass);
    CHECK(v[6].state == WindowState::kFail);  // trusted camera, flow disagrees
}

TEST_CASE("Verifier: one own-value trigger alone is suspect, not a failure (REJ-32)") {
    // Window 2 strokes with a sixth of the usual amplitude; flow and
    // foreground agree with it. Only the writer's own range (c8) is odd.
    auto y = Stroke(900);
    const auto small = Stroke(900, 5);
    for (int i = 120; i < 180; ++i) y[static_cast<size_t>(i)] = small[static_cast<size_t>(i)];
    const auto v = VerifyWindows(y, Healthy(y), VerifyOptions{});
    CHECK(v[2].c8 < 0.25);
    CHECK(v[2].c1 > 0.4);
    CHECK(v[2].state == WindowState::kSuspect);
    CHECK(v[2].reasons.find("c8") != std::string::npos);
    CHECK(Count(v, WindowState::kPass) == 14);
}

TEST_CASE("Verifier: windows never span a scene cut") {
    // After the cut at frame 450 the box sits on background: the flow inside
    // it is noise and nothing moves there. The cut frames themselves look
    // like huge motion to the flow and the background model, so a window
    // across the cut can pass by accident; per-shot windows do not.
    auto y = Stroke(900);
    auto c = Healthy(y);
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0, 0.3);
    for (int i = 450; i < 900; ++i) {
        y[static_cast<size_t>(i)] = 28 + noise(rng);
        c.box_flow[static_cast<size_t>(i)] = noise(rng);
        c.foreground[static_cast<size_t>(i)] = 0.0;
    }
    for (int i = 450; i < 453; ++i) {  // the cut: everything "moves"
        c.box_flow[static_cast<size_t>(i)] = -45;
        c.foreground[static_cast<size_t>(i)] = 1.0;
    }
    VerifyOptions o;
    o.window_frames = 60;
    o.skip_frames = 20;  // 20 + k*60 puts a window across the cut without cut info
    auto v = VerifyWindows(y, c, o);
    bool across = false;
    for (const auto& w : v) across = across || (w.first < 450 && w.end > 450);
    CHECK(across);

    o.cuts = {450};
    v = VerifyWindows(y, c, o);
    for (const auto& w : v) {
        CHECK_FALSE((w.first < 450 && w.end > 450));
        if (w.first >= 450) {
            CHECK(w.first >= 455);
            CHECK(w.state == WindowState::kFail);
        } else {
            CHECK(w.state == WindowState::kPass);
        }
    }
    // the shot before the cut ends in a short tail, joined to its last window
    CHECK(v[6].end == 450);
}

TEST_CASE("Verifier: a box pinned at the picture border fails although flow agrees") {
    // Owner clip, 26 Oct: CSRT drifted to the border after 20 s and kept
    // finding motion there; flow and foreground passed 102 such windows.
    CHECK(BoxOutsideShare(10, 10, 40, 40, 256, 144) == doctest::Approx(0.0));
    CHECK(BoxOutsideShare(-20, 10, 40, 40, 256, 144) == doctest::Approx(0.5));
    CHECK(BoxOutsideShare(236, 124, 40, 40, 256, 144) == doctest::Approx(0.75));
    CHECK(std::isnan(BoxOutsideShare(0, 0, 0, 40, 256, 144)));

    const auto y = Stroke(900);
    auto c = Healthy(y);
    c.box_outside.assign(y.size(), 0.0);
    for (int i = 300; i < 420; ++i) c.box_outside[static_cast<size_t>(i)] = 0.5;
    for (int i = 600; i < 660; ++i) c.box_outside[static_cast<size_t>(i)] = 0.2;  // touching, not pinned
    const auto v = VerifyWindows(y, c, VerifyOptions{});
    CHECK(v[5].state == WindowState::kFail);
    CHECK(v[5].reasons.find("c9") != std::string::npos);
    CHECK(v[10].state == WindowState::kPass);
    CHECK(Count(v, WindowState::kPass) == 13);
}

TEST_CASE("Verifier: missing checkers are unknown, never a pass by default") {
    const auto y = Stroke(300);
    const auto v = VerifyWindows(y, FrameChecks{}, VerifyOptions{});
    REQUIRE(v.size() == 5);
    CHECK(std::isnan(v[0].c1));
    CHECK(std::isnan(v[0].c2));
    CHECK(std::isnan(v[0].c4));
    CHECK(v[0].state == WindowState::kPass);  // the writer alone has nothing against it
    CHECK(std::string(WindowStateName(WindowState::kStill)) == "still");
}
