#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "samn/script_quality.h"

using samn::Action;

// 0.7 funscript/script_quality_test.go
TEST_CASE("Device compat: actions too close are flagged") {
    const std::vector<Action> a{{0, 0}, {50, 50}, {100, 0}, {150, 50}, {200, 0}, {1000, 50}};
    CHECK_FALSE(samn::EvaluateDeviceCompat(a).warnings.empty());
}

TEST_CASE("Device compat: a clean script has no warnings") {
    const auto r = samn::EvaluateDeviceCompat({{0, 10}, {400, 90}, {800, 10}, {1200, 90}});
    CHECK(r.warnings.empty());
    CHECK(r.metrics.position_span >= 70);
}

// GAP-12: the metric values themselves. Expected numbers are worked out by
// hand and equal 0.7 funscript.EvaluateDeviceCompat on the same actions.
TEST_CASE("Device compat: slow full strokes count moves of 50+ over 900 ms") {
    // Moves: 80/1000 ms slow, 80/500, 80/1500 slow, 70/400, 60/900 (exactly
    // 900 is not slow), 49/2000 (under 50 is not a large move).
    const auto r = samn::EvaluateDeviceCompat(
        {{0, 10}, {1000, 90}, {1500, 10}, {3000, 90}, {3400, 20}, {4300, 80}, {6300, 31}});
    CHECK(r.metrics.slow_full_strokes == 2);
    CHECK(r.metrics.actions_too_close == 0);
    CHECK(r.metrics.position_span == doctest::Approx(80.0));
    CHECK(r.metrics.avg_intensity == doctest::Approx(33.3));
    CHECK(r.metrics.peak_intensity == doctest::Approx(85.6));
    // 2 of 5 large moves are slow (> 25 %): one warning naming the count.
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("2 large moves take longer than 900 ms") != std::string::npos);
}

TEST_CASE("Device compat: a quarter of slow full strokes is not yet a warning") {
    const auto r = samn::EvaluateDeviceCompat({{0, 10}, {1000, 90}, {1400, 10}, {1800, 90}, {2200, 10}});
    CHECK(r.metrics.slow_full_strokes == 1);  // 1 of 4: the warning needs more than 25 %
    CHECK(r.warnings.empty());
    CHECK(r.metrics.avg_intensity == doctest::Approx(72.7));
    CHECK(r.metrics.peak_intensity == doctest::Approx(100.0));
}

TEST_CASE("Device compat: positions outside 5-95 are counted, 5 and 95 are usable") {
    const auto r = samn::EvaluateDeviceCompat(
        {{0, 0}, {400, 5}, {800, 95}, {1200, 100}, {1600, 4}, {2000, 96}, {2400, 50}});
    CHECK(r.metrics.actions_outside_usable_range == 4);  // 0, 100, 4, 96
    CHECK(r.metrics.position_span == doctest::Approx(100.0));
    CHECK(r.metrics.avg_intensity == doctest::Approx(69.6));
    CHECK(r.metrics.peak_intensity == doctest::Approx(118.8));
    CHECK(r.warnings.empty());  // the count is reported, not warned about (0.7)
}

TEST_CASE("Device compat: intensity is 500 x |dpos| / dt, time-weighted, peak at the 95th percentile") {
    // Intensities 250, 25, 300 over 100, 200, 100 ms, then a zero-length
    // step (intensity 0, no weight). Average (25000 + 5000 + 30000) / 400;
    // peak between 250 and 300 of [0, 25, 250, 300] at rank 2.85.
    const auto r = samn::EvaluateDeviceCompat({{0, 0}, {100, 50}, {300, 60}, {400, 0}, {400, 20}});
    CHECK(r.metrics.avg_intensity == doctest::Approx(150.0));
    CHECK(r.metrics.peak_intensity == doctest::Approx(292.5));
    CHECK(r.metrics.actions_too_close == 0);  // exactly 100 ms and 0 ms are not "too close"
    CHECK(r.metrics.too_close_share == doctest::Approx(0.0));
    CHECK(r.metrics.actions_outside_usable_range == 2);
    CHECK(r.metrics.position_span == doctest::Approx(60.0));
}

TEST_CASE("Script quality: fewer than 2 actions") {
    const auto r = samn::EvaluateScriptQuality({{0, 50}});
    CHECK_FALSE(r.passed);
    CHECK(r.score == 0);
    CHECK(r.estimated_from_script_only);
    REQUIRE(r.warnings.size() == 1);
}

TEST_CASE("Script quality: out of range, unsorted, duplicates, gaps") {
    auto has = [](const samn::ScriptQualityResult& r, const char* s) {
        for (const auto& w : r.warnings)
            if (w.find(s) != std::string::npos) return true;
        return false;
    };
    const auto oor = samn::EvaluateScriptQuality({{0, -10}, {500, 150}, {1000, 50}});
    CHECK(has(oor, "outside 0-100"));
    const auto uns = samn::EvaluateScriptQuality({{500, 10}, {0, 90}, {1000, 10}});
    CHECK(has(uns, "not in ascending order"));
    const auto dup = samn::EvaluateScriptQuality({{0, 10}, {0, 90}, {400, 10}});
    CHECK(has(dup, "duplicate timestamps"));
    std::vector<Action> gap;
    for (int i = 0; i < 20; ++i) gap.push_back({i * 400, i % 2 ? 90 : 10});
    gap.push_back({20 * 400 + 30000, 90});
    CHECK(has(samn::EvaluateScriptQuality(gap), "time gap"));
}

TEST_CASE("Script quality: kind and a healthy script passes") {
    std::vector<Action> ok;
    for (int i = 0; i < 40; ++i) ok.push_back({i * 450, i % 2 ? 92 : 8});
    const auto r = samn::EvaluateScriptQuality(ok);
    CHECK(r.kind == "signal_quality");
    CHECK(r.passed);
    CHECK(r.score == doctest::Approx(1.0));
    CHECK(r.warnings.empty());
}

TEST_CASE("Script quality of a file keeps the file order; .samn general; errors") {
    const auto dir = std::filesystem::temp_directory_path() / "samn_sq_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream(dir / "u.funscript") << R"({"actions":[{"at":500,"pos":10},{"at":0,"pos":90},{"at":1000,"pos":10}]})";
        std::ofstream(dir / "g.samn") << R"({"general":[{"at":0,"pos":10},{"at":400,"pos":90}]})";
        std::ofstream(dir / "e.funscript") << R"({"actions":[]})";
        std::ofstream(dir / "bad.funscript") << "{nope";
    }
    const auto u = samn::ScriptQualityOfFile(dir / "u.funscript");
    CHECK(u.warnings.front() == "Timestamps are not in ascending order");
    CHECK(samn::ScriptQualityOfFile(dir / "g.samn").kind == "signal_quality");
    CHECK_THROWS(samn::ScriptQualityOfFile(dir / "e.funscript"));
    CHECK_THROWS(samn::ScriptQualityOfFile(dir / "bad.funscript"));
    CHECK_THROWS(samn::ScriptQualityOfFile(dir / "missing.funscript"));
    std::filesystem::remove_all(dir);
}
