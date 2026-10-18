#include <doctest/doctest.h>

#include <map>

#include "samn/ozone.h"

using namespace samn;

namespace {
// A flat baseline with raised stretches at the given [from, to) windows (ms).
std::vector<Action> WithBumps(int64_t total_ms, int baseline, const std::map<std::pair<int64_t, int64_t>, int>& bumps) {
    std::vector<Action> out;
    for (int64_t at = 0; at <= total_ms; at += 100) {
        int pos = baseline;
        for (const auto& [w, p] : bumps)
            if (at >= w.first && at < w.second) pos = p;
        out.push_back({at, pos});
    }
    return out;
}
}  // namespace

// Ported from 0.7 funscript/ozone_test.go.
TEST_CASE("ozone: picks the late peak") {
    std::vector<Action> a;
    for (int i = 0; i <= 100; ++i) a.push_back({i * 100, i >= 90 ? 90 : 20});
    const OZoneSuggestion s = SuggestOZone(a);
    REQUIRE(s.ok);
    CHECK(s.start_ms >= 8000);
    CHECK(s.end_ms > s.start_ms);
    CHECK(s.reason.find("highest mean position") != std::string::npos);
}

TEST_CASE("ozone: a flat tail gives none") {
    std::vector<Action> a;
    for (int i = 0; i <= 50; ++i) a.push_back({i * 100, 20});
    const OZoneSuggestion s = SuggestOZone(a);
    CHECK(!s.ok);
    CHECK(s.reason.find("flat") != std::string::npos);
}

TEST_CASE("ozone: a short script gives none") {
    CHECK(!SuggestOZone({{0, 0}, {100, 100}, {200, 0}, {300, 50}}).ok);
    CHECK(!SuggestOZone({{0, 0}, {5000, 100}}).ok);
}

TEST_CASE("ozone: finds an earlier, weaker rise") {
    const auto a = WithBumps(100000, 10, {{{90000, 100001}, 90}, {{20000, 25000}, 55}});
    const OZoneSuggestion p = SuggestOZone(a);
    REQUIRE(p.ok);
    const auto sec = SuggestSecondaryOZones(a, p, 2);
    REQUIRE(!sec.empty());
    CHECK(sec[0].start_ms < p.start_ms);
    CHECK(sec[0].mean < p.mean);
    CHECK(sec[0].start_ms < 25000);
    CHECK(sec[0].end_ms > 20000);
}

TEST_CASE("ozone: max count, no overlap, none without a primary") {
    const auto a = WithBumps(120000, 10, {{{100000, 110001}, 90}, {{10000, 15000}, 55}, {{40000, 45000}, 60}, {{70000, 75000}, 50}});
    const OZoneSuggestion p = SuggestOZone(a);
    REQUIRE(p.ok);
    CHECK(SuggestSecondaryOZones(a, p, 1).size() <= 1);
    CHECK(SuggestSecondaryOZones(a, p, 2).size() <= 2);
    CHECK(SuggestSecondaryOZones(a, p, 0).empty());

    const auto wide = WithBumps(120000, 10, {{{100000, 110001}, 90}, {{10000, 20000}, 60}});
    const auto sec = SuggestSecondaryOZones(wide, SuggestOZone(wide), 3);
    for (size_t i = 0; i < sec.size(); ++i)
        for (size_t j = i + 1; j < sec.size(); ++j)
            CHECK(!(sec[i].start_ms < sec[j].end_ms && sec[j].start_ms < sec[i].end_ms));
    for (size_t i = 1; i < sec.size(); ++i) CHECK(sec[i - 1].start_ms < sec[i].start_ms);

    CHECK(SuggestSecondaryOZones(WithBumps(50000, 10, {}), OZoneSuggestion{}, 2).empty());
}

TEST_CASE("ozone: a flat lead-in gives no secondary") {
    const auto a = WithBumps(100000, 10, {{{90000, 100001}, 90}});
    const OZoneSuggestion p = SuggestOZone(a);
    REQUIRE(p.ok);
    CHECK(SuggestSecondaryOZones(a, p, 2).empty());
}

// 0.7 app_generator.go applyAutoOZoneMarker / secondaryMarkerFromSuggestion.
TEST_CASE("ozone: the automatic markers after Create") {
    const auto a = WithBumps(100000, 10, {{{90000, 100001}, 90}, {{20000, 25000}, 55}});
    const auto m = AutoOZoneMarkers(a);
    REQUIRE(m.size() == 2);
    CHECK(m[0].kind == OMarkerPrimary);
    CHECK(m[0].intensity == 1.0);
    CHECK(m[1].kind == OMarkerSecondary);
    CHECK(m[1].intensity >= 0.2);
    CHECK(m[1].intensity <= 0.8);
    for (const auto& x : m) CHECK_NOTHROW(x.Validate());
    CHECK(AutoOZoneMarkers(WithBumps(50000, 10, {})).empty());
}
