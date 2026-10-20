// Phase suggestions without a model (D-157).
#include <doctest/doctest.h>

#include "samn/phase_rules.h"

namespace {
// A synthetic clip, 500 ms per step: a black start, a calm intro, the action,
// a louder and busier peak, a calm end and black again.
samn::PhaseSignals StoryClip() {
    samn::PhaseSignals s;
    s.step_ms = 500;
    const auto add = [&](int seconds, double luma, double motion, double loud) {
        for (int i = 0; i < seconds * 2; ++i) {
            s.luma.push_back(luma);
            s.motion.push_back(motion);
            s.loudness.push_back(loud);
        }
    };
    add(4, 0.02, 0.0, 0.0);     // black
    add(16, 0.5, 0.02, 0.2);    // intro: talk, little motion
    add(70, 0.5, 0.20, 0.3);    // the action
    add(15, 0.5, 0.35, 0.9);    // the peak
    add(13, 0.5, 0.05, 0.1);    // calm after it
    add(2, 0.01, 0.0, 0.0);     // black
    return s;
}
std::string NameAt(const std::vector<samn::LabelPhase>& phases, int64_t ms) {
    for (const auto& p : phases)
        if (ms >= p.start_ms && ms < p.end_ms) return p.name;
    return "";
}
}  // namespace

TEST_CASE("the rules find empty, intro, climax and cool-down") {
    const auto phases = samn::SuggestPhases(StoryClip());
    REQUIRE_FALSE(phases.empty());
    for (const auto& p : phases) {
        CHECK_FALSE(p.confirmed);
        CHECK(p.source == "rules");
    }
    CHECK(NameAt(phases, 1000) == "empty");
    CHECK(NameAt(phases, 10000) == "intro");
    CHECK(NameAt(phases, 50000) == "");  // the action itself: the user names it
    CHECK(NameAt(phases, 97000) == "climax");
    CHECK(NameAt(phases, 112000) == "cool-down");
    CHECK(NameAt(phases, 119000) == "empty");
    // The intro ends where the action begins, give or take the smoothing.
    for (const auto& p : phases)
        if (p.name == "intro") CHECK(std::llabs(p.end_ms - 20000) <= 3000);
}

TEST_CASE("an even clip has no climax, a silent one still works") {
    samn::PhaseSignals s;
    s.step_ms = 500;
    s.luma.assign(240, 0.5);
    s.motion.assign(240, 0.2);
    const auto phases = samn::SuggestPhases(s);
    for (const auto& p : phases) CHECK(p.name != "climax");
    CHECK(samn::SuggestPhases({}).empty());
    // All black: one empty stretch and nothing else.
    samn::PhaseSignals dark;
    dark.luma.assign(20, 0.0);
    dark.motion.assign(20, 0.0);
    const auto d = samn::SuggestPhases(dark);
    REQUIRE(d.size() == 1);
    CHECK(d[0].name == "empty");
}

TEST_CASE("suggestions never overwrite what the user set") {
    samn::LabelProject p;
    p.duration_ms = 120000;
    p.SetPhase(90000, 100000, "oral");  // the user's own
    const auto added = samn::ApplyPhaseSuggestions(p, samn::SuggestPhases(StoryClip()));
    CHECK(added > 0);
    CHECK(p.PhaseAt(95000)->name == "oral");
    CHECK(p.PhaseAt(95000)->confirmed);
    CHECK(p.PhaseAt(10000)->name == "intro");
    CHECK_FALSE(p.PhaseAt(10000)->confirmed);
    // Suggesting again changes nothing.
    const auto before = p.phases.size();
    samn::ApplyPhaseSuggestions(p, samn::SuggestPhases(StoryClip()));
    CHECK(p.phases.size() == before);
}

TEST_CASE("the video stage skips intro and empty and notices the climax once") {
    const std::vector<samn::LabelPhase> phases{{0, 10000, "intro", true, "user"},
                                               {10000, 12000, "empty", true, "user"},
                                               {30000, 40000, "empty", false, "rules"},  // a suggestion: plays
                                               {60000, 70000, "climax", true, "user"}};
    auto s = samn::StepVideoPhases(phases, 500, -1);
    CHECK(s.seek_to == 12000);  // across the touching empty stretch
    CHECK(s.climax == -1);
    CHECK(samn::StepVideoPhases(phases, 20000, -1).seek_to == -1);
    CHECK(samn::StepVideoPhases(phases, 35000, -1).seek_to == -1);
    s = samn::StepVideoPhases(phases, 61000, -1);
    CHECK(s.climax == 3);
    CHECK(samn::StepVideoPhases(phases, 62000, 3).climax == -1);  // once per climax
    CHECK(samn::StepVideoPhases({}, 1000, -1).seek_to == -1);
}
