#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "samn/script_analysis.h"

using samn::Action;

namespace {

std::vector<Action> ZigzagThenStill() {
    std::vector<Action> actions;
    for (int i = 0; i < 40; ++i) actions.push_back({i * 250, i % 2 == 0 ? 90 : 10});
    for (int i = 0; i < 40; ++i) actions.push_back({10000 + i * 250, 50});
    return actions;
}

std::vector<Action> SteadyMotion() {
    std::vector<Action> actions;
    for (int i = 0; i < 80; ++i) actions.push_back({i * 250, i % 2 == 0 ? 90 : 10});
    return actions;
}

}  // namespace

// 0.7 app_analysis_test.go TestAnalyzeScriptFindsStaticStretches.
TEST_CASE("AnalyzeScript finds still stretches a flat line would hide") {
    const auto analysis = samn::AnalyzeScriptActions(ZigzagThenStill());
    CHECK(analysis.error.empty());
    CHECK_FALSE(analysis.segments.empty());
    const auto still = analysis.share_by_state.find("static");
    REQUIRE(still != analysis.share_by_state.end());
    CHECK(still->second >= 0.25);
    CHECK(analysis.summary.find("still") != std::string::npos);
    // Share is above 30%, so the extra sentence from summarizeStates is present.
    CHECK(analysis.summary.find("long stretches without motion") != std::string::npos);
    double total = 0;
    for (const auto& item : analysis.share_by_state) total += item.second;
    // 0.7 allows 0.95..1.05: segment ends sit on a sample, so the gaps
    // between runs are not in any share.
    CHECK(total >= 0.95);
    CHECK(total <= 1.05);
}

// 0.7 TestAnalyzeScriptWithoutScript, plus three actions (the limit is 4).
TEST_CASE("AnalyzeScript rejects fewer than 4 actions") {
    const auto none = samn::AnalyzeScriptActions({});
    CHECK(none.error == "no script loaded (or too few points)");
    CHECK(none.summary.empty());
    CHECK(none.segments.empty());
    const auto three = samn::AnalyzeScriptActions({{0, 10}, {250, 90}, {500, 10}});
    CHECK(three.error == "no script loaded (or too few points)");
    CHECK(three.summary.empty());
}

// 0.7 TestAnalyzeScriptSteadyMotion.
TEST_CASE("AnalyzeScript does not call steady motion still") {
    const auto analysis = samn::AnalyzeScriptActions(SteadyMotion());
    CHECK(analysis.error.empty());
    const auto still = analysis.share_by_state.find("static");
    const double share = still == analysis.share_by_state.end() ? 0 : still->second;
    CHECK(share <= 0.1);
}

TEST_CASE("Chapter labels and the m:ss clock match 0.7") {
    CHECK(samn::MotionChapterLabel("pause") == "Pause");
    CHECK(samn::MotionChapterLabel("build") == "Build-up");
    CHECK(samn::MotionChapterLabel("steady") == "steady");
    CHECK(samn::MotionChapterLabel("crescendo") == "Crescendo");
    CHECK(samn::MotionChapterLabel("winddown") == "Wind-down");
    CHECK(samn::MotionChapterLabel("Warmup") == "Warmup");
    CHECK(samn::MotionChapterLabel("chapter") == "chapter");
    // formatMs rounds half away from zero, then m:ss (minutes may pass 59).
    CHECK(samn::FormatAnalysisClock(0) == "0:00");
    CHECK(samn::FormatAnalysisClock(499) == "0:00");
    CHECK(samn::FormatAnalysisClock(500) == "0:01");
    CHECK(samn::FormatAnalysisClock(4000) == "0:04");
    CHECK(samn::FormatAnalysisClock(4001) == "0:04");
    CHECK(samn::FormatAnalysisClock(90000) == "1:30");
    CHECK(samn::FormatAnalysisClock(125000) == "2:05");
}
