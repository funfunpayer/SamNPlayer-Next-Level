#include <doctest/doctest.h>

// The Generator against the behaviour spec (docs/GENERATOR_SPEC.md): one test
// case per spec test id, named "spec Tn/Un/Xn: ...". Where SamNPlayer keeps
// 0.7's behaviour on purpose the case pins that behaviour and says so; the
// opt-in switches (gap handling, outliers, end time, analysis rate, track
// cache) are also checked to change nothing while they are off.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "samn/engine/generate.h"
#include "samn/engine/post_track.h"
#include "samn/engine/signal_chain.h"
#include "samn/engine/track_box.h"
#include "samn/engine/track_cache.h"
#include "synthetic_clips.h"

using namespace samn::engine;
namespace clips = samn::engine::testclips;
namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;

fs::path TempDir(const char* name) {
    const fs::path p = fs::temp_directory_path() / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// draw(frame, t_seconds) paints one frame on the textured background.
template <class Draw>
bool WriteClip(const fs::path& p, double fps, double seconds, Draw draw) {
    cv::VideoWriter w(p.u8string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, {clips::kW, clips::kH}, true);
    if (!w.isOpened()) return false;
    const cv::Mat bg = clips::Texture(clips::kW, clips::kH, 11, 140);
    for (int i = 0; i < int(std::lround(fps * seconds)); ++i) {
        cv::Mat f = bg.clone();
        draw(f, i / fps);
        w.write(f);
    }
    return true;
}

// The disc's centre moves y = 120 + amp/2 sin(2 pi f t). Image y grows
// downwards, so a script peak (high position) is a disc minimum: t = 0.75/f.
double DiscY(double t, double amp, double hz) { return 120 + amp / 2 * std::sin(2 * kPi * hz * t); }

struct Recorder {
    std::mutex m;
    std::vector<GenerateEvent> events;
    GenerateCallback fn() {
        return [this](const GenerateEvent& e) {
            std::lock_guard<std::mutex> l(m);
            events.push_back(e);
        };
    }
    std::vector<GenerateEvent> take() {
        std::lock_guard<std::mutex> l(m);
        return events;
    }
    bool Noted(const std::string& prefix) {
        for (const auto& e : take())
            if (e.kind == GenerateEventKind::kNote && e.message.rfind(prefix, 0) == 0) return true;
        return false;
    }
};

GenerateRequest Request(const fs::path& dir, const char* clip, const char* out) {
    GenerateRequest r;
    r.video = dir / clip;
    r.box = cv::Rect(100, 40, 120, 160);
    r.signal.profile = "standard";
    r.output = dir / out;
    r.auto_ozone = false;
    return r;
}

std::vector<samn::Action> ReadActions(const fs::path& p) {
    std::ifstream in(p);
    const nlohmann::json j = nlohmann::json::parse(in);
    std::vector<samn::Action> out;
    for (const auto& a : j["actions"]) out.push_back({a["at"].get<int64_t>(), a["pos"].get<int>()});
    return out;
}

std::string ReadText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<samn::Action> Generated(const GenerateRequest& req, Recorder* rec = nullptr) {
    Recorder local;
    Recorder& r = rec ? *rec : local;
    Generate(req, 1, {}, r.fn());
    const auto ev = r.take();
    REQUIRE(!ev.empty());
    REQUIRE_MESSAGE(ev.back().kind == GenerateEventKind::kDone, ev.back().message);
    return ReadActions(req.output);
}

// The turning points: every action but the first and the last.
std::vector<samn::Action> Extrema(const std::vector<samn::Action>& a) {
    if (a.size() < 3) return {};
    return std::vector<samn::Action>(a.begin() + 1, a.end() - 1);
}

// Distance in ms from t to the nearest time phase/hz + k/hz.
double PhaseError(double t_ms, double hz, double phase) {
    const double period = 1000.0 / hz, first = phase * period;
    const double k = std::round((t_ms - first) / period);
    return std::abs(t_ms - (first + k * period));
}

// U8 on every script a spec case produces.
void CheckWellFormed(const std::vector<samn::Action>& a) {
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].pos >= 0);
        CHECK(a[i].pos <= 100);
        if (i > 0) CHECK(a[i].at > a[i - 1].at);
    }
}

void CheckSine(const std::vector<samn::Action>& a, double hz, int expected_extrema, bool inverted = false) {
    CheckWellFormed(a);
    const auto ex = Extrema(a);
    CHECK(std::abs(int(ex.size()) - expected_extrema) <= 1);
    for (const auto& e : ex) {
        const bool high = e.pos >= 50;
        // Script peak = disc at its top (y minimum, 0.75 of a period); inverted swaps them.
        const double phase = high != inverted ? 0.75 : 0.25;
        CHECK_MESSAGE(PhaseError(double(e.at), hz, phase) <= 40.0, "at " << e.at << " pos " << e.pos);
        if (high) CHECK(e.pos >= 90);
        else CHECK(e.pos <= 10);
    }
}

std::vector<double> Times(int n, double step_ms) {
    std::vector<double> t(n);
    for (int i = 0; i < n; ++i) t[i] = i * step_ms;
    return t;
}

}  // namespace

// ---------------------------------------------------------------- end to end

TEST_CASE("spec T1: a 1 Hz sine gives its peaks and valleys on time") {
    const fs::path dir = TempDir("samn_spec_t1");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 10, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    const auto a = Generated(Request(dir, "sine.mp4", "t1.funscript"));
    CheckSine(a, 1.0, 20);
    fs::remove_all(dir);
}

TEST_CASE("spec T2: invert mirrors every position at the same times") {
    const fs::path dir = TempDir("samn_spec_t2");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 10, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    const auto plain = Generated(Request(dir, "sine.mp4", "plain.funscript"));
    GenerateRequest req = Request(dir, "sine.mp4", "inv.funscript");
    req.signal.invert = true;
    const auto inv = Generated(req);
    REQUIRE(inv.size() == plain.size());
    for (size_t i = 0; i < inv.size(); ++i) {
        CHECK(inv[i].at == plain[i].at);
        CHECK(std::abs(inv[i].pos - (100 - plain[i].pos)) <= 1);
    }
    CheckSine(inv, 1.0, 20, true);
    fs::remove_all(dir);
}

TEST_CASE("spec T3: horizontal motion on the horizontal axis") {
    const fs::path dir = TempDir("samn_spec_t3");
    // x = 160 + 40 sin: left-most (x minimum) at 0.75 s like the vertical case.
    REQUIRE(WriteClip(dir / "side.mp4", 30, 10,
                      [](cv::Mat& f, double t) { clips::Disc(f, int(160 + 40 * std::sin(2 * kPi * t)), 120); }));
    GenerateRequest req = Request(dir, "side.mp4", "t3.funscript");
    req.box = cv::Rect(100, 60, 120, 120);
    req.axis = TrackAxis::kX;
    CheckSine(Generated(req), 1.0, 20);
    fs::remove_all(dir);
}

TEST_CASE("spec T4: irregular sample times (frames dropped) keep the timing") {
    // The container cannot be made VFR with OpenCV's writer, so the drop is
    // done on the track: 30 % of a 60 fps track removed at random.
    const fs::path dir = TempDir("samn_spec_t4");
    REQUIRE(WriteClip(dir / "sine.mp4", 60, 10, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    TrackOptions to;
    to.axis = TrackAxis::kY;
    std::string error;
    const auto tr = TrackBox(dir / "sine.mp4", cv::Rect(100, 40, 120, 160), to, &error);
    REQUIRE_MESSAGE(tr, error);
    std::mt19937 rng(4);
    std::vector<double> t, y;
    for (size_t i = 0; i < tr->times_ms.size(); ++i)
        if (i == 0 || i + 1 == tr->times_ms.size() || std::uniform_real_distribution<double>(0, 1)(rng) >= 0.3) {
            t.push_back(double(tr->times_ms[i]));
            y.push_back(tr->positions[i]);
        }
    CHECK(t.size() < tr->times_ms.size() * 0.8);
    SignalOptions so = ApplyDefaultDetrend(SignalOptions{}, std::nullopt, nullptr);
    so.profile = "standard";
    const auto sig = RunSignal(t, y, so, &error);
    REQUIRE_MESSAGE(sig, error);
    CheckSine(sig->actions, 1.0, 20);
    fs::remove_all(dir);
}

TEST_CASE("spec T5/U7: no motion is an error, not a flat script (0.7 behaviour kept)") {
    // The spec asks for two points at 50; SamNPlayer keeps 0.7's clear error
    // ("no discernible motion ... check the box"), which the Generator shows.
    const fs::path dir = TempDir("samn_spec_t5");
    REQUIRE(WriteClip(dir / "still.mp4", 30, 5, [](cv::Mat& f, double) { clips::Disc(f, 160, 120); }));
    Recorder rec;
    Generate(Request(dir, "still.mp4", "t5.funscript"), 1, {}, rec.fn());
    const auto last = rec.take().back();
    CHECK(last.kind == GenerateEventKind::kError);
    CHECK(last.message.find("no discernible motion") != std::string::npos);
    CHECK(!fs::exists(dir / "t5.funscript"));
    fs::remove_all(dir);
}

namespace {
// 1 Hz disc that vanishes between gap_from and gap_to seconds.
bool WriteGapClip(const fs::path& p, double gap_from, double gap_to) {
    return WriteClip(p, 30, 10, [&](cv::Mat& f, double t) {
        if (t < gap_from || t >= gap_to) clips::Disc(f, 160, int(DiscY(t, 80, 1)));
    });
}
}  // namespace

TEST_CASE("spec T6: a long gap gets no actions when gap handling is on") {
    const fs::path dir = TempDir("samn_spec_t6");
    REQUIRE(WriteGapClip(dir / "gap.mp4", 4.0, 6.0));
    GenerateRequest req = Request(dir, "gap.mp4", "t6.funscript");
    req.scene_cuts = false;  // the vanishing disc is no cut; keep the case about the gap
    req.signal.max_gap_ms = 500;
    req.signal.gaps_from_look = true;  // CSRT keeps a box on the empty frame (GD-11)
    Recorder rec;
    const auto a = Generated(req, &rec);
    CheckWellFormed(a);
    CHECK(rec.Noted("POST: "));
    // The coast (8 frames, about 270 ms) may run into the gap; after that, nothing.
    for (const auto& e : a) CHECK_FALSE_MESSAGE((e.at > 4400 && e.at < 5900), "action inside the gap at " << e.at);
    // The strokes on both sides are still there and on time.
    int before = 0, after = 0;
    for (const auto& e : Extrema(a)) {
        if (e.at < 4000) ++before;
        if (e.at > 6100) ++after;
    }
    CHECK(before >= 7);
    CHECK(after >= 6);

    // Without the switch the same clip writes held positions through the gap
    // (0.7): the switch is what removes them.
    GenerateRequest off = Request(dir, "gap.mp4", "off.funscript");
    off.scene_cuts = false;
    const auto held = Generated(off);
    bool inside = false;
    for (const auto& e : held) inside = inside || (e.at > 4400 && e.at < 5900);
    CHECK(inside);
    fs::remove_all(dir);
}

TEST_CASE("spec T7: a short gap is bridged and the stroke count stays") {
    const fs::path dir = TempDir("samn_spec_t7");
    REQUIRE(WriteGapClip(dir / "gap.mp4", 4.1, 4.4));
    GenerateRequest req = Request(dir, "gap.mp4", "t7.funscript");
    req.scene_cuts = false;
    req.signal.max_gap_ms = 500;
    const auto a = Generated(req);
    CheckWellFormed(a);
    CHECK(std::abs(int(Extrema(a).size()) - 20) <= 2);
    fs::remove_all(dir);
}

TEST_CASE("spec T8: strokes too fast for the frame rate are warned about") {
    const fs::path dir = TempDir("samn_spec_t8");
    REQUIRE(WriteClip(dir / "fast.mp4", 10, 10, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 40, 4))); }));
    Recorder rec;
    GenerateRequest req = Request(dir, "fast.mp4", "t8.funscript");
    Generate(req, 1, {}, rec.fn());
    CHECK(rec.Noted("analysis rate too low"));

    // T1's clip is not warned about.
    REQUIRE(WriteClip(dir / "slow.mp4", 30, 4, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    Recorder calm;
    Generated(Request(dir, "slow.mp4", "slow.funscript"), &calm);
    CHECK_FALSE(calm.Noted("analysis rate too low"));
    fs::remove_all(dir);
}

TEST_CASE("spec T9: slow drift is removed by the default detrend") {
    const fs::path dir = TempDir("samn_spec_t9");
    REQUIRE(WriteClip(dir / "drift.mp4", 30, 10, [](cv::Mat& f, double t) {
        clips::Disc(f, 160, int(DiscY(t, 80, 1) - 30 + 6 * t));  // 60 px down over 10 s
    }));
    const auto a = Generated(Request(dir, "drift.mp4", "t9.funscript"));
    CheckWellFormed(a);
    const auto ex = Extrema(a);
    CHECK(ex.size() >= 18);
    for (const auto& e : ex) {
        if (e.pos >= 50) CHECK(e.pos >= 85);
        else CHECK(e.pos <= 15);
    }
    fs::remove_all(dir);
}

TEST_CASE("spec T10: a time range limits the script to that range") {
    const fs::path dir = TempDir("samn_spec_t10");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 10, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    GenerateRequest req = Request(dir, "sine.mp4", "t10.funscript");
    req.start_ms = 2000;
    req.end_ms = 5000;
    const auto a = Generated(req);
    CheckWellFormed(a);
    REQUIRE(!a.empty());
    CHECK(a.front().at >= 2000);
    CHECK(a.back().at <= 5000);
    CHECK(std::abs(int(Extrema(a).size()) - 6) <= 1);

    // An end before the start is refused before any work.
    req.end_ms = 1000;
    req.overwrite = true;  // else the overwrite guard answers first
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    CHECK(rec.take().back().kind == GenerateEventKind::kError);
    CHECK(rec.take().back().message.find("end time") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("spec T11: cancel at half way stops within a second and keeps the old script") {
    const fs::path dir = TempDir("samn_spec_t11");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 20, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    std::ofstream(dir / "t11.funscript") << "previous";
    GenerateRequest req = Request(dir, "sine.mp4", "t11.funscript");
    req.overwrite = true;
    std::atomic<bool> half{false};
    std::chrono::steady_clock::time_point asked;
    std::vector<GenerateEvent> ev;
    Generate(
        req, 1, [&] { return half.load(); },
        [&](const GenerateEvent& e) {
            ev.push_back(e);
            if (e.kind == GenerateEventKind::kProgress && e.percent >= 50 && !half) {
                asked = std::chrono::steady_clock::now();
                half = true;
            }
        });
    REQUIRE(half);
    CHECK(ev.back().kind == GenerateEventKind::kCanceled);
    CHECK(std::chrono::steady_clock::now() - asked < std::chrono::seconds(1));
    CHECK(ReadText(dir / "t11.funscript") == "previous");
    fs::remove_all(dir);
}

TEST_CASE("spec T12: the same input gives a byte-identical script") {
    const fs::path dir = TempDir("samn_spec_t12");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 6, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    Generated(Request(dir, "sine.mp4", "a.funscript"));
    Generated(Request(dir, "sine.mp4", "b.funscript"));
    CHECK(ReadText(dir / "a.funscript") == ReadText(dir / "b.funscript"));
    fs::remove_all(dir);
}

// ------------------------------------------------------- post-processing units

TEST_CASE("spec U1: a single spike is ignored with outlier rejection, and only with it") {
    const auto t = Times(300, 33.3);
    std::vector<double> clean(300), spiked;
    for (int i = 0; i < 300; ++i) clean[i] = 120 + 40 * std::sin(2 * kPi * t[i] / 1000.0);
    spiked = clean;
    spiked[137] += 400;  // 10x the amplitude, one frame
    SignalOptions o;
    o.detrend_window_ms = 3000;
    std::string error;
    const auto base = RunSignal(t, clean, o, &error);
    REQUIRE(base);
    o.outlier_mad_factor = 5;
    const auto fixed = RunSignal(t, spiked, o, &error);
    REQUIRE(fixed);
    CHECK(fixed->outliers == 1);
    REQUIRE(fixed->actions.size() == base->actions.size());
    for (size_t i = 0; i < base->actions.size(); ++i) {
        CHECK(std::abs(double(fixed->actions[i].at - base->actions[i].at)) <= 34);
        CHECK(std::abs(fixed->actions[i].pos - base->actions[i].pos) <= 3);
    }
    o.outlier_mad_factor = 0;
    const auto raw = RunSignal(t, spiked, o, &error);
    REQUIRE(raw);
    bool differs = raw->actions.size() != base->actions.size();
    for (size_t i = 0; !differs && i < base->actions.size(); ++i)
        differs = std::abs(raw->actions[i].pos - base->actions[i].pos) > 3;
    CHECK(differs);
}

TEST_CASE("spec U2: gaps of 200 ms are bridged, gaps of 900 ms split") {
    const auto t = Times(200, 50);
    std::vector<double> y(200);
    for (int i = 0; i < 200; ++i) y[i] = i;
    std::vector<uint8_t> lost(200, 0);
    for (int i = 50; i < 53; ++i) lost[i] = 1;   // 49 → 53: 200 ms
    for (int i = 120; i < 137; ++i) lost[i] = 1;  // 119 → 137: 900 ms
    GapOptions go;
    go.max_gap_ms = 500;
    const GapPlan plan = PlanGaps(t, y, lost, go);
    CHECK(plan.bridged == 1);
    CHECK(plan.split == 1);
    REQUIRE(plan.segments.size() == 2);
    CHECK(plan.segments[0] == std::make_pair(0, 120));
    CHECK(plan.segments[1] == std::make_pair(137, 200));
    CHECK(plan.values[51] == doctest::Approx(51));  // the line through the gap
    // Lost lead-in and tail: short ones are held, long ones dropped.
    std::vector<uint8_t> edges(200, 0);
    edges[0] = edges[1] = 1;
    for (int i = 180; i < 200; ++i) edges[i] = 1;
    const GapPlan e = PlanGaps(t, y, edges, go);
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0] == std::make_pair(0, 180));
    CHECK(e.values[0] == 2);
    // All lost: nothing to post-process.
    CHECK(PlanGaps(t, y, std::vector<uint8_t>(200, 1), go).segments.empty());
}

TEST_CASE("spec U3: a small ripple makes no points with a prominence floor") {
    const auto t = Times(300, 33.3);
    std::vector<double> pure(300), rippled(300);
    for (int i = 0; i < 300; ++i) {
        pure[i] = 100 * std::sin(2 * kPi * t[i] / 1000.0);
        rippled[i] = pure[i] + 3 * std::sin(2 * kPi * 2.7 * t[i] / 1000.0);
    }
    PostOptions o;
    o.peak_prominence = 0.1;  // 10 of 100 position units
    std::string error;
    const auto a = PositionsToActions(t, pure, o, &error), b = PositionsToActions(t, rippled, o, &error);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(b->actions.size() == a->actions.size());
}

TEST_CASE("spec U4: points closer than the minimum interval are thinned") {
    const auto t = Times(400, 10);
    std::vector<double> y(400);
    for (int i = 0; i < 400; ++i) y[i] = std::sin(2 * kPi * 5 * t[i] / 1000.0) + 0.3 * std::sin(2 * kPi * 16 * t[i] / 1000.0);
    PostOptions o;
    o.smooth_window = 1;
    o.min_peak_distance_ms = 10;
    o.min_interval_ms = 60;
    std::string error;
    const auto r = PositionsToActions(t, y, o, &error);
    REQUIRE(r);
    for (size_t i = 2; i + 1 < r->actions.size(); ++i) CHECK(r->actions[i].at - r->actions[i - 1].at >= 60);
}

TEST_CASE("spec U5: the speed limit caps the change, times stay (from the previous point, 0.7)") {
    // The spec centres the shortened move on its midpoint; SamNPlayer keeps
    // 0.7's limit, which moves from the previous point.
    int changed = 0;
    const auto out = LimitSpeed({{0, 0}, {100, 100}}, 500, &changed);
    CHECK(changed == 1);
    REQUIRE(out.size() == 2);
    CHECK(out[1].at == 100);
    CHECK(std::abs(out[1].pos - out[0].pos) <= 50);
}

TEST_CASE("spec U6: a straight line keeps only its ends") {
    const auto t = Times(100, 33);
    std::vector<double> y(100);
    for (int i = 0; i < 100; ++i) y[i] = i;
    PostOptions o;
    o.rdp_tolerance = 2;
    std::string error;
    const auto r = PositionsToActions(t, y, o, &error);
    REQUIRE(r);
    REQUIRE(r->actions.size() == 2);
    CHECK(r->actions.front().at == 0);
    CHECK(r->actions.back().at == 99 * 33);
}

TEST_CASE("spec U9: smoothing does not shift the turning points") {
    const auto t = Times(300, 33.3);
    std::vector<double> y(300);
    for (int i = 0; i < 300; ++i) y[i] = std::sin(2 * kPi * t[i] / 1000.0);
    std::string error;
    const auto r = PositionsToActions(t, y, PostOptions{}, &error);
    REQUIRE(r);
    // Rising signal = falling script: script peaks at the signal's minima (0.75 s).
    for (const auto& e : Extrema(r->actions))
        CHECK(PhaseError(double(e.at), 1.0, e.pos >= 50 ? 0.75 : 0.25) <= 34);
}

TEST_CASE("spec E8 unit: the aliasing check reads the stroke rate against the sample rate") {
    auto sine = [](double hz, double fps, double seconds) {
        std::vector<double> t, y;
        for (int i = 0; i < int(fps * seconds); ++i) {
            t.push_back(i * 1000.0 / fps);
            y.push_back(100 + 40 * std::sin(2 * kPi * hz * i / fps));
        }
        return std::make_pair(t, y);
    };
    const auto [t1, y1] = sine(4, 10, 10);
    CHECK(AliasingWarning(t1, y1));
    const auto [t2, y2] = sine(1, 30, 10);
    CHECK_FALSE(AliasingWarning(t2, y2));
    const auto [t3, y3] = sine(3, 10, 10);  // 0.3 x the rate: still fine
    CHECK_FALSE(AliasingWarning(t3, y3));
    CHECK_FALSE(AliasingWarning({0, 40, 80}, {1, 2, 3}));  // too short to say
}

// ------------------------------------------------------- errors and files

TEST_CASE("spec X1/X2: a missing or broken video is an error, not a crash") {
    const fs::path dir = TempDir("samn_spec_x1");
    Recorder rec;
    Generate(Request(dir, "missing.mp4", "x.funscript"), 1, {}, rec.fn());
    CHECK(rec.take().back().kind == GenerateEventKind::kError);
    std::ofstream(dir / "text.mp4") << "this is not a video";
    Recorder rec2;
    Generate(Request(dir, "text.mp4", "x.funscript"), 1, {}, rec2.fn());
    CHECK(rec2.take().back().kind == GenerateEventKind::kError);
    CHECK(!fs::exists(dir / "x.funscript"));
    fs::remove_all(dir);
}

TEST_CASE("spec X6: a script that cannot be written is an error naming the problem") {
    const fs::path dir = TempDir("samn_spec_x6");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 3, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    GenerateRequest req = Request(dir, "sine.mp4", "x6.funscript");
    req.output = dir / "no" / "such" / "folder" / "x6.funscript";
    Recorder rec;
    Generate(req, 1, {}, rec.fn());
    const auto last = rec.take().back();
    CHECK(last.kind == GenerateEventKind::kError);
    CHECK(last.message.rfind("cannot write the script", 0) == 0);
    fs::remove_all(dir);
}

// ------------------------------------------------ the opt-in switches themselves

TEST_CASE("spec switches: all off gives the same script as before") {
    const fs::path dir = TempDir("samn_spec_off");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 6, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    TrackOptions to;
    std::string error;
    const auto tr = TrackBox(dir / "sine.mp4", cv::Rect(100, 40, 120, 160), to, &error);
    REQUIRE_MESSAGE(tr, error);
    REQUIRE(tr->lost.size() == tr->times_ms.size());
    const std::vector<double> t(tr->times_ms.begin(), tr->times_ms.end());
    const SignalOptions so = ApplyDefaultDetrend(SignalOptions{}, std::nullopt, nullptr);
    const auto a = RunSignal(t, tr->positions, so, &error);
    std::vector<uint8_t> all_lost(t.size(), 1);  // ignored while max_gap_ms is 0
    const auto b = RunSignal(t, tr->positions, all_lost, so, &error);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->actions.size() == b->actions.size());
    for (size_t i = 0; i < a->actions.size(); ++i) {
        CHECK(a->actions[i].at == b->actions[i].at);
        CHECK(a->actions[i].pos == b->actions[i].pos);
    }
    CHECK(a->post.dense_pos == b->post.dense_pos);
    fs::remove_all(dir);
}

TEST_CASE("spec FR-7: the analysis rate cap tracks fewer frames over the same span") {
    const fs::path dir = TempDir("samn_spec_rate");
    REQUIRE(WriteClip(dir / "sine.mp4", 60, 4, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    TrackOptions to;
    to.max_analysis_fps = 30;
    int last_done = 0, last_total = 0;
    to.on_progress = [&](int done, int total) {
        last_done = done;
        last_total = total;
    };
    std::string error;
    const auto tr = TrackBox(dir / "sine.mp4", cv::Rect(100, 40, 120, 160), to, &error);
    REQUIRE_MESSAGE(tr, error);
    CHECK(std::abs(int(tr->times_ms.size()) - 120) <= 2);
    for (size_t i = 1; i < tr->times_ms.size(); ++i) CHECK(tr->times_ms[i] - tr->times_ms[i - 1] >= 32);
    CHECK(tr->times_ms.back() >= 3900);
    CHECK(last_done >= last_total - 2);  // progress follows the video, not the samples
    fs::remove_all(dir);
}

TEST_CASE("spec FR-11: the cache key covers adaptive cuts and the seed, and never serves recorded checks") {
    const fs::path dir = TempDir("samn_spec_cache_key");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 2, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    const cv::Rect box(100, 40, 120, 160);
    const fs::path video = dir / "sine.mp4";
    TrackOptions to;
    const std::string key = TrackCacheKey(video, box, to);
    REQUIRE(key.size() == 64);
    TrackOptions other = to;
    other.adaptive_cuts = true;
    CHECK(TrackCacheKey(video, box, other) != key);
    other = to;
    other.seed_box = cv::Rect(5, 6, 30, 30);
    CHECK(TrackCacheKey(video, box, other) != key);
    other.seed_gray = cv::Mat(8, 8, CV_8UC1, cv::Scalar(10));
    const std::string seeded = TrackCacheKey(video, box, other);
    CHECK(seeded != key);
    TrackOptions another = other;
    another.seed_gray = cv::Mat(8, 8, CV_8UC1, cv::Scalar(11));
    CHECK(TrackCacheKey(video, box, another) != seeded);
    CHECK(TrackCacheKey(video, box, other) == seeded);
    other = to;
    other.record_checks = true;
    CHECK(TrackCacheKey(video, box, other).empty());  // no key, no cache
    fs::remove_all(dir);
}

TEST_CASE("spec FR-11: the track cache returns the same script and misses on any option change") {
    const fs::path dir = TempDir("samn_spec_cache");
    REQUIRE(WriteClip(dir / "sine.mp4", 30, 4, [](cv::Mat& f, double t) { clips::Disc(f, 160, int(DiscY(t, 80, 1))); }));
    const cv::Rect box(100, 40, 120, 160);
    TrackOptions to;
    const std::string key = TrackCacheKey(dir / "sine.mp4", box, to);
    REQUIRE(key.size() == 64);
    CHECK(TrackCacheKey(dir / "sine.mp4", box, to) == key);  // stable
    TrackOptions other = to;
    other.camera_compensation = false;
    CHECK(TrackCacheKey(dir / "sine.mp4", box, other) != key);
    other = to;
    other.end_ms = 2000;
    CHECK(TrackCacheKey(dir / "sine.mp4", box, other) != key);
    CHECK(TrackCacheKey(dir / "sine.mp4", cv::Rect(101, 40, 120, 160), to) != key);
    CHECK(TrackCacheKey(dir / "missing.mp4", box, to).empty());

    GenerateRequest req = Request(dir, "sine.mp4", "first.funscript");
    req.track_cache_dir = dir / "cache";
    Recorder first;
    Generated(req, &first);
    CHECK_FALSE(first.Noted("tracking: from the cache"));
    req.output = dir / "second.funscript";
    Recorder second;
    Generated(req, &second);
    CHECK(second.Noted("tracking: from the cache"));
    CHECK(ReadText(dir / "first.funscript") == ReadText(dir / "second.funscript"));

    // A broken entry is a miss, not an error.
    for (const auto& f : fs::directory_iterator(dir / "cache")) std::ofstream(f.path()) << "{broken";
    CHECK_FALSE(LoadTrackCache(dir / "cache", key));
    req.output = dir / "third.funscript";
    Recorder third;
    Generated(req, &third);
    CHECK_FALSE(third.Noted("tracking: from the cache"));
    CHECK(ReadText(dir / "first.funscript") == ReadText(dir / "third.funscript"));
    fs::remove_all(dir);
}
