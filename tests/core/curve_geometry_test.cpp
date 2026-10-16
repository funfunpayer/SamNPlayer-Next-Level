#include <doctest/doctest.h>

#include "samn/curve_geometry.h"

using samn::Action;

namespace {
std::vector<Action> Zigzag() {
    std::vector<Action> a;
    for (int i = 0; i < 400; ++i) a.push_back({i * 90 + (i % 7) * 13, (i * 37) % 101});
    return a;
}
}  // namespace

TEST_CASE("curve geometry: Catmull-Rom control points as 0.7, clamped to 0-100") {
    const std::vector<Action> a{{0, 0}, {100, 100}, {200, 0}, {300, 100}};
    const auto s = samn::CatmullRomBezier(a, 0, a.size() - 1);
    REQUIRE(s.size() == 3);
    // First piece: p0 = p1 (no neighbour before).
    CHECK(s[0].c1.at == doctest::Approx(100.0 / 6));
    CHECK(s[0].c1.pos == doctest::Approx(100.0 / 6));
    CHECK(s[0].c2.at == doctest::Approx(100 - 200.0 / 6));
    CHECK(s[0].c2.pos == 100);  // 100 - (0 - 0)/6
    // Middle piece: 100 + (0 - 0)/6 = 100, 0 - (100 - 100)/6 = 0.
    CHECK(s[1].c1.pos == 100);
    CHECK(s[1].c2.pos == 0);
    // Overshoot is clamped.
    const std::vector<Action> b{{0, 100}, {100, 100}, {200, 0}};
    for (const auto& seg : samn::CatmullRomBezier(b, 0, 2)) {
        CHECK(seg.c1.pos >= 0);
        CHECK(seg.c1.pos <= 100);
        CHECK(seg.c2.pos >= 0);
        CHECK(seg.c2.pos <= 100);
    }
    CHECK(samn::CatmullRomBezier({{0, 1}}, 0, 0).empty());
}

TEST_CASE("curve geometry: a window uses its outside neighbours for the tangents") {
    const auto a = Zigzag();
    const auto full = samn::CatmullRomBezier(a, 0, a.size() - 1);
    const auto part = samn::CatmullRomBezier(a, 100, 120);
    REQUIRE(part.size() == 20);
    CHECK(part.front().c1 == full[100].c1);
    CHECK(part.back().c2 == full[119].c2);
}

TEST_CASE("curve geometry: visible range and position") {
    const std::vector<Action> a{{0, 0}, {100, 100}, {200, 0}, {300, 100}, {400, 0}};
    CHECK(samn::VisibleRange(a, 150, 250) == std::pair<size_t, size_t>{1, 3});
    CHECK(samn::VisibleRange(a, 0, 400) == std::pair<size_t, size_t>{0, 4});
    CHECK(samn::VisibleRange(a, 1000, 2000).first <= samn::VisibleRange(a, 1000, 2000).second);
    CHECK(samn::VisibleRange({}, 0, 1).first > samn::VisibleRange({}, 0, 1).second);
    CHECK(*samn::PositionAt(a, -5) == 0);
    CHECK(*samn::PositionAt(a, 50) == doctest::Approx(50));
    CHECK(*samn::PositionAt(a, 250) == doctest::Approx(50));
    CHECK(*samn::PositionAt(a, 999) == 0);
    CHECK_FALSE(samn::PositionAt({}, 0).has_value());
    CHECK(samn::CountInWindow(a, 100, 300) == 3);
}

TEST_CASE("curve geometry: density switch as 0.7") {
    CHECK_FALSE(samn::IsDense(2, 10, samn::kDotMinGapPx));
    CHECK(samn::IsDense(100, 800, samn::kDotMinGapPx));        // 8 px a point
    CHECK_FALSE(samn::IsDense(100, 800, samn::kEditMinGapPx));  // editing keeps them
    CHECK(samn::SpeedBinPx(400, 800) == 5.0);
    CHECK(samn::SpeedBinPx(4000, 800) == 4.0);
}

TEST_CASE("curve geometry: tempo wave equals the 0.7 drawSpeedWave") {
    // Reference values from 0.7's playback.js run in node on the same input.
    const auto w = samn::SpeedWave(Zigzag(), 0, 40000, 800, 6);
    REQUIRE(w.size() == 120);
    CHECK(w[0].at == 3);
    CHECK(w[0].pos == doctest::Approx(66.92961165048544));
    CHECK(w[10].at == 63);
    CHECK(w[10].pos == doctest::Approx(86.07456471987821));
    CHECK(w[50].at == 303);
    CHECK(w[50].pos == doctest::Approx(88.10893849116333));
    CHECK(w.back().at == 717);
    CHECK(w.back().pos == doctest::Approx(95.99035679359608));
}

TEST_CASE("view window: zoom as 0.7") {
    const int64_t total = 60000;
    const auto full = samn::FullView(total);
    CHECK(full == samn::ViewWindow{0, 60000});
    CHECK_FALSE(samn::IsZoomed(full, total));
    // In by 1.25 around 30 s: centred, 48 s wide.
    const auto in = samn::ZoomView(full, total, 1.25, 30000.0);
    CHECK(in == samn::ViewWindow{6000, 54000});
    CHECK(samn::IsZoomed(in, total));
    // The anchor keeps its place: at 1/4 of the window.
    const auto a = samn::ZoomView({0, 40000}, total, 2, 10000.0);
    CHECK(a == samn::ViewWindow{5000, 25000});
    // Pushed back inside at the edges.
    CHECK(samn::ZoomView({0, 40000}, total, 2, 0.0) == samn::ViewWindow{0, 20000});
    CHECK(samn::ZoomView({20000, 60000}, total, 2, 60000.0) == samn::ViewWindow{40000, 60000});
    // Never narrower than 250 ms.
    samn::ViewWindow v = full;
    for (int i = 0; i < 100; ++i) v = samn::ZoomView(v, total, 1.6, 1000.0);
    CHECK(v.end_ms - v.start_ms == samn::kMinViewSpanMs);
    // Zooming out to the full span resets.
    CHECK(samn::ZoomView(in, total, 1 / 1.6) == full);
    // Without an anchor: the centre.
    CHECK(samn::ZoomView(full, total, 2) == samn::ViewWindow{15000, 45000});
    // Broken windows become the full one.
    CHECK(samn::ClampView({500, 100}, total) == full);
    CHECK(samn::ClampView({-5, 99999}, total) == full);
}

TEST_CASE("view window: zoom to a range with a margin") {
    CHECK(samn::ZoomToRange(10000, 20000, 60000) == samn::ViewWindow{9200, 20800});
    CHECK(samn::ZoomToRange(100, 600, 60000) == samn::ViewWindow{0, 800});
    CHECK(samn::ZoomToRange(5, 5, 60000) == samn::FullView(60000));
}

TEST_CASE("timeline: ruler steps and labels as 0.7") {
    // 60 s on 800 px: 1 s = 13 px, 5 s = 67 px, 10 s = 133 px -> 10 s.
    CHECK(samn::RulerStepMs(60000, 800) == 10000);
    CHECK(samn::RulerStepMs(10000, 800) == 1000);
    CHECK(samn::RulerStepMs(3.6e6, 800) == 600000);
    CHECK(samn::RulerStepMs(1e9, 800) == 3600000);
    CHECK(samn::FormatRulerTime(0) == "00:00");
    CHECK(samn::FormatRulerTime(65400) == "01:05");
    CHECK(samn::FormatRulerTime(3725000) == "1:02:05");
}

TEST_CASE("timeline: tooltip time, with 0.7's tenths overflow fixed") {
    CHECK(samn::FormatTooltipTime(0) == "0.0s");
    CHECK(samn::FormatTooltipTime(12340) == "12.3s");
    CHECK(samn::FormatTooltipTime(59960) == "1:00.0");
    CHECK(samn::FormatTooltipTime(61960) == "1:02.0");  // 0.7: "1:01.10"
    CHECK(samn::FormatTooltipTime(754300) == "12:34.3");
    CHECK(samn::FormatTooltipTime(-5) == "0.0s");
}
