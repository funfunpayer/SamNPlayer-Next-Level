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
