#include <doctest/doctest.h>

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "samn/engine/post_track.h"

using namespace samn::engine;
using samn::Action;

#ifndef SAMN_ENGINE_TESTDATA
#error "SAMN_ENGINE_TESTDATA must name tests/engine/testdata"
#endif

namespace {

// Synthetic goldens made by 0.7's generate_funscript.py (sine, outlier,
// noise, damped ringing, speed limit, dynamic range), shared with 0.7's
// posttrack tests.
const nlohmann::json& Goldens() {
    static const nlohmann::json all = [] {
        std::ifstream in(std::string(SAMN_ENGINE_TESTDATA) + "/posttrack_goldens.json");
        return nlohmann::json::parse(in);
    }();
    REQUIRE(all.size() == 14);
    return all;
}

const nlohmann::json& Case(const nlohmann::json& all, const std::string& name) {
    for (const auto& c : all)
        if (c["name"] == name) return c;
    FAIL("missing golden " << name);
    return all;
}

PostOptions FromKwargs(const nlohmann::json& kw) {
    PostOptions o;
    if (kw.is_null()) return o;
    if (kw.contains("invert")) o.invert = kw["invert"].get<bool>();
    if (kw.contains("smooth_window")) o.smooth_window = kw["smooth_window"].get<int>();
    if (kw.contains("min_peak_distance_ms")) o.min_peak_distance_ms = kw["min_peak_distance_ms"].get<int>();
    if (kw.contains("rdp_tolerance")) o.rdp_tolerance = kw["rdp_tolerance"].get<double>();
    if (kw.contains("norm_percentile")) o.norm_percentile = kw["norm_percentile"].get<double>();
    if (kw.contains("adaptive_error")) o.adaptive_error = kw["adaptive_error"].get<double>();
    if (kw.contains("min_interval_ms")) o.min_interval_ms = kw["min_interval_ms"].get<double>();
    if (kw.contains("dynamic_range_ms")) o.dynamic_range_ms = kw["dynamic_range_ms"].get<double>();
    if (kw.contains("peak_prominence")) o.peak_prominence = kw["peak_prominence"].get<double>();
    return o;
}

std::vector<Action> ActionsOf(const nlohmann::json& a) {
    std::vector<Action> out;
    for (const auto& x : a) out.push_back({x["at"].get<int64_t>(), x["pos"].get<int>()});
    return out;
}

PostResult Run(const nlohmann::json& c) {
    std::string error;
    auto r = PositionsToActions(c["timestamps_ms"].get<std::vector<double>>(), c["positions"].get<std::vector<double>>(),
                                FromKwargs(c["kwargs"]), &error);
    REQUIRE_MESSAGE(r, error);
    return *r;
}

}  // namespace

// Ported from 0.7 posttrack/convert_test.go.
TEST_CASE("post: every golden gives the same actions and dense curve as 0.7") {
    int cases = 0;
    for (const auto& c : Goldens()) {
        if (c.contains("kind") && c["kind"] == "limit_speed") continue;
        CAPTURE(c["name"].get<std::string>());
        const PostResult r = Run(c);
        const std::vector<Action> want = ActionsOf(c["actions"]);
        REQUIRE(r.actions.size() == want.size());
        for (size_t i = 0; i < want.size(); ++i) {
            CHECK(r.actions[i].at == want[i].at);
            CHECK(r.actions[i].pos == want[i].pos);
        }
        const auto dense = c["dense_pos"].get<std::vector<double>>();
        REQUIRE(r.dense_pos.size() == dense.size());
        double max_diff = 0;
        for (size_t i = 0; i < dense.size(); ++i) max_diff = std::max(max_diff, std::abs(r.dense_pos[i] - dense[i]));
        CHECK(max_diff <= 0.05);
        ++cases;
    }
    CHECK(cases == 13);
}

TEST_CASE("post: the speed limit equals 0.7") {
    const auto& c = Case(Goldens(), "limit_speed_200");
    int changed = -1;
    const auto out = LimitSpeed(ActionsOf(c["actions_in"]), c["max_speed"].get<double>(), &changed);
    CHECK(changed == c["changed"].get<int>());
    const auto want = ActionsOf(c["actions_out"]);
    REQUIRE(out.size() == want.size());
    for (size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].at == want[i].at);
        CHECK(out[i].pos == want[i].pos);
    }
}

TEST_CASE("post: prominence quiets damped ringing") {
    const auto& all = Goldens();
    const PostResult without = Run(Case(all, "damped_no_prom")), with = Run(Case(all, "damped_prom035"));
    CHECK(without.actions.size() > 70);
    CHECK(with.actions.size() < without.actions.size());
}

TEST_CASE("post: percentile normalisation beats min/max on an outlier") {
    const auto& all = Goldens();
    auto span = [](const std::vector<double>& d) {
        double lo = 100, hi = 0;
        for (size_t i = 0; i < d.size(); ++i) {
            if (i >= 20 && i < 26) continue;  // the spike frames
            lo = std::min(lo, d[i]);
            hi = std::max(hi, d[i]);
        }
        return hi - lo;
    };
    CHECK(span(Run(Case(all, "outlier_percentile")).dense_pos) > span(Run(Case(all, "outlier_minmax")).dense_pos));
}

// Ported from 0.7 posttrack/filters_test.go.
TEST_CASE("post: detrend removes slow drift and keeps the stroke") {
    std::vector<double> x(200);
    for (int i = 0; i < 200; ++i) x[i] = i * 0.5 + 10 * std::sin(i / 5.0);
    const auto out = RollingDetrend(x, 800, 40);
    double sum = 0;
    for (double v : out) sum += v;
    CHECK(std::abs(sum / 200) <= 2);
    CHECK(*std::max_element(out.begin(), out.end()) - *std::min_element(out.begin(), out.end()) >= 10);
}

TEST_CASE("post: bandpass removes DC and keeps the stroke band") {
    const double fs = 30, pi = 3.14159265358979323846;
    std::vector<double> x(512);
    for (int i = 0; i < 512; ++i) {
        const double t = i / fs;
        x[i] = 50 + 20 * std::sin(2 * pi * 1.5 * t) + 5 * std::sin(2 * pi * 10 * t);
    }
    const auto out = Bandpass(x, fs, 0.5, 4.0);
    double sum = 0;
    for (double v : out) sum += v;
    CHECK(std::abs(sum / 512) <= 8);
    CHECK(*std::max_element(out.begin(), out.end()) - *std::min_element(out.begin(), out.end()) >= 20);
}

TEST_CASE("post: a flat track is an error, not a script") {
    std::string error;
    CHECK(!PositionsToActions({0, 33, 66, 100, 133, 166}, {5, 5, 5, 5, 5, 5}, PostOptions{}, &error));
    CHECK(error.find("no discernible motion") != std::string::npos);
    CHECK(!PositionsToActions({0, 33}, {1}, PostOptions{}, &error));
}

TEST_CASE("post: scenes are normalised each on their own") {
    std::vector<double> t, y;
    for (int i = 0; i < 120; ++i) {
        t.push_back(i * 33.3);
        // A big stroke, then a small one (a wider shot).
        y.push_back(100 + (i < 60 ? 80 : 10) * std::sin(i / 4.0));
    }
    PostOptions o;
    o.scene_ranges = {{0, 60}, {60, 120}};
    std::string error;
    const auto r = PositionsToActions(t, y, o, &error);
    REQUIRE_MESSAGE(r, error);
    auto span = [&](int a, int b) {
        const auto [lo, hi] = std::minmax_element(r->dense_pos.begin() + a, r->dense_pos.begin() + b);
        return *hi - *lo;
    };
    CHECK(span(5, 55) > 90);
    // The small scene gets most of the range (the scene's 2 % percentile
    // still sees the smoothed edge of the cut) instead of a sliver.
    CHECK(span(65, 115) > 70);
    const auto whole = PositionsToActions(t, y, PostOptions{}, &error);
    REQUIRE(whole);
    const auto [lo, hi] = std::minmax_element(whole->dense_pos.begin() + 65, whole->dense_pos.begin() + 115);
    CHECK(*hi - *lo < 20);
}

TEST_CASE("post: samples outside every scene range stay within 0..100") {
    std::vector<double> t, y;
    for (int i = 0; i < 120; ++i) {
        t.push_back(i * 33.3);
        y.push_back(100 + 80 * std::sin(i / 4.0));
    }
    PostOptions o;
    o.scene_ranges = {{0, 60}};  // the second half belongs to no scene
    std::string error;
    const auto r = PositionsToActions(t, y, o, &error);
    REQUIRE_MESSAGE(r, error);
    REQUIRE(!r->actions.empty());
    for (const auto& a : r->actions) {
        CHECK(a.pos >= 0);
        CHECK(a.pos <= 100);
    }
    for (const double p : r->dense_pos) {
        CHECK(p >= 0);
        CHECK(p <= 100);
    }
}

TEST_CASE("post: clamp keeps positions in range") {
    const auto out = ClampActionsPos({{0, -5}, {100, 50}, {200, 130}}, 10, 90);
    CHECK(out[0].pos == 10);
    CHECK(out[1].pos == 50);
    CHECK(out[2].pos == 90);
}
