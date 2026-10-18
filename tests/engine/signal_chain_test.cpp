#include <doctest/doctest.h>

#include <cmath>

#include "samn/engine/signal_chain.h"

using namespace samn::engine;

// Ported from 0.7 generator/detrend_default_test.go.
TEST_CASE("signal: detrend window is two stroke periods, clamped") {
    auto ms = [](std::optional<StrokeHint> h) { return AdaptiveDetrendMs(h, nullptr); };
    CHECK(ms(StrokeHint{true, 1.0}) == 2000);   // 1 Hz: the best-scoring window on the long clip
    CHECK(ms(StrokeHint{true, 1.5}) == 1500);   // 1333 clamps up to the floor
    CHECK(ms(StrokeHint{true, 0.4}) == 4000);   // slow stroke: capped so it cannot swallow it
    CHECK(ms(StrokeHint{false, 1.0}) == 3000);  // unreliable tempo
    CHECK(ms(StrokeHint{true, 5.0}) == 3000);
    CHECK(ms(StrokeHint{true, 0}) == 3000);
    CHECK(ms(std::nullopt) == 3000);
}

TEST_CASE("signal: default detrend respects the caller and distance profiles") {
    const StrokeHint hint{true, 1.0};
    SignalOptions o;
    std::string note;
    CHECK(ApplyDefaultDetrend(o, hint, &note).detrend_window_ms == 2000);
    CHECK(note == "POST: detrend 2000ms (2x stroke period at 1.00 Hz)");
    o.profile = "autotune";
    CHECK(ApplyDefaultDetrend(o, hint, nullptr).detrend_window_ms == 2000);
    o = SignalOptions{};
    o.detrend_window_ms = 5000;
    CHECK(ApplyDefaultDetrend(o, hint, nullptr).detrend_window_ms == 5000);
    o.detrend_window_ms = -1;
    CHECK(ApplyDefaultDetrend(o, hint, nullptr).detrend_window_ms == -1);
    for (const char* p : {"tj", "tf"}) {
        o = SignalOptions{};
        o.profile = p;
        CHECK(ApplyDefaultDetrend(o, hint, nullptr).detrend_window_ms == 0);
    }
}

TEST_CASE("signal: profile recipes fill only what the user left open") {
    SignalOptions o;
    PostOptions p = BuildPostOptions(o, 0, 0, 0, 0);
    CHECK(p.smooth_window == 11);
    CHECK(p.min_peak_distance_ms == 150);
    CHECK(p.norm_percentile == 2.0);
    CHECK(p.min_interval_ms == 100.0);
    CHECK(p.peak_prominence == 0);

    o.profile = "weich";
    p = BuildPostOptions(o, 0, 0, 0, 0);
    CHECK(p.peak_prominence == 0.35);
    CHECK(p.min_peak_distance_ms == 200);
    CHECK(p.dynamic_range_ms == 3000);
    CHECK(BuildPostOptions(o, 0, 120, 0, 0).min_peak_distance_ms == 120);

    o.profile = "autotune";
    p = BuildPostOptions(o, 0, 0, 0, 0);
    CHECK(p.detrend_window_ms == 3000);
    CHECK(p.bandpass_low_hz == 0.5);
    CHECK(p.bandpass_high_hz == 4.0);
    CHECK(p.dynamic_range_ms == 3000);
    CHECK(p.peak_prominence == 0.2);
    o.detrend_window_ms = -1;  // explicitly off stays off
    CHECK(BuildPostOptions(o, 0, 0, 0, 0).detrend_window_ms == -1);

    o = SignalOptions{};
    o.min_action_interval_ms = -1;
    CHECK(BuildPostOptions(o, 0, 0, 0, -1).min_interval_ms == 0);
    CHECK(BuildPostOptions(o, 0, 0, 0, -1).norm_percentile == 0);
}

TEST_CASE("signal: autotune caps the speed, distance profiles clamp 20-90") {
    std::vector<double> t, y;
    for (int i = 0; i < 300; ++i) {
        t.push_back(i * 33.0);
        y.push_back(200 + 100 * std::sin(i / 1.5));  // fast, ~3 Hz
    }
    std::string error;
    SignalOptions o;
    o.profile = "autotune";
    auto r = RunSignal(t, y, o, &error);
    REQUIRE_MESSAGE(r, error);
    for (size_t i = 1; i < r->actions.size(); ++i) {
        const double dt = double(r->actions[i].at - r->actions[i - 1].at);
        CHECK(std::abs(r->actions[i].pos - r->actions[i - 1].pos) <= 400 * dt / 1000 + 0.5);
    }
    o = SignalOptions{};
    o.profile = "tj";
    r = RunSignal(t, y, o, &error);
    REQUIRE_MESSAGE(r, error);
    for (const auto& a : r->actions) {
        CHECK(a.pos >= 20);
        CHECK(a.pos <= 90);
    }
    CHECK(r->post.dense_pos.size() == t.size());
}
