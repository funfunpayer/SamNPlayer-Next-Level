// Port of SamNPlayer 0.7 funscript/improve_test.go (the polish behind
// "Optimize for" and the post-Create step).
#include <doctest/doctest.h>

#include <stdexcept>

#include "samn/improve.h"

using samn::Action;
using samn::TrackingGap;

TEST_CASE("TrimActions cuts in the middle and interpolates the ends") {
    const std::vector<Action> in{{0, 0}, {1000, 100}, {2000, 0}, {3000, 50}};
    const auto out = samn::TrimActions(in, 500, 2500);
    CHECK(out.front().at == 500);
    CHECK(out.back().at == 2500);
    CHECK(out.front().pos == 50);  // halfway 0 → 100
    CHECK_THROWS_AS(samn::TrimActions({{0, 0}}, 0, 10), std::invalid_argument);
}

TEST_CASE("FillGaps inserts linear points") {
    int gaps = 0, added = 0;
    const auto out = samn::FillGaps({{0, 0}, {2000, 100}}, 500, 500, 0, &gaps, &added);
    CHECK(gaps == 1);
    CHECK(added >= 1);
    CHECK(out.front().at == 0);
    CHECK(out.back().at == 2000);
    bool mid = false;
    for (const auto& a : out)
        if (a.at == 1000) {
            mid = true;
            CHECK(a.pos == 50);
        }
    CHECK(mid);
}

TEST_CASE("FillGaps spaces points by the audio tempo") {
    int added_default = 0, added_audio = 0;
    samn::FillGaps({{0, 0}, {2000, 100}}, 500, 0, 0, nullptr, &added_default);
    samn::FillGaps({{0, 0}, {2000, 100}}, 500, 0, 1.0, nullptr, &added_audio);  // 1 Hz → 500 ms
    CHECK(added_default > 0);
    CHECK(added_audio > 0);
    CHECK(added_audio < added_default);
}

TEST_CASE("ImproveScript auto mode fills a medium hole in a second pass") {
    const std::vector<Action> in{{0, 0}, {100, 20}, {3000, 80}, {3100, 90}, {3600, 40}, {3700, 50}};
    samn::ImproveOptions o;
    o.fill_gaps = true;
    const auto res = samn::ImproveScript(in, o);
    CHECK(res.gaps_filled >= 2);
    CHECK(res.points_added >= 2);
}

TEST_CASE("ImproveScript does not densify a slow stroke") {
    std::vector<Action> in;
    for (int i = 0; i < 20; ++i) in.push_back({int64_t(i) * 500, i % 2 ? 85 : 20});
    samn::ImproveOptions o;
    o.fill_gaps = true;
    const auto res = samn::ImproveScript(in, o);
    CHECK(res.points_added == 0);
    CHECK(res.gaps_filled == 0);
    CHECK(res.after_count == int(in.size()));
}

TEST_CASE("ImproveScript trims and fills") {
    samn::ImproveOptions o;
    o.start_ms = 50;
    o.end_ms = 5050;
    o.fill_gaps = true;
    o.max_gap_ms = 800;
    o.step_ms = 200;
    const auto res = samn::ImproveScript({{0, 0}, {100, 10}, {5000, 90}, {5100, 100}}, o);
    CHECK(res.trimmed);
    CHECK(res.gaps_filled >= 1);
    CHECK(res.after_count > res.before_count);
}

TEST_CASE("HealTrackingGaps strips the interior and bridges") {
    const std::vector<Action> in{{0, 0}, {100, 10}, {1000, 99}, {1500, 98}, {2000, 90}, {2100, 100}};
    int healed = 0, added = 0;
    const auto out = samn::HealTrackingGaps(in, {{500, 1800}}, 200, 0, &healed, &added);
    CHECK(healed == 1);
    CHECK(added >= 1);
    for (const auto& a : out) CHECK_FALSE((a.at > 500 && a.at < 1800 && (a.pos == 99 || a.pos == 98)));
    CHECK(out.front().at == 0);
    CHECK(out.back().at == 2100);
}

TEST_CASE("HealTrackingGaps keeps the boundary anchors") {
    const std::vector<Action> in{{0, 0}, {500, 20}, {1000, 99}, {1500, 80}, {2000, 100}};
    int healed = 0;
    const auto out = samn::HealTrackingGaps(in, {{500, 1500}}, 100, 0, &healed);
    CHECK(healed >= 1);
    bool has500 = false, has1500 = false;
    for (const auto& a : out) {
        if (a.at == 500 && a.pos == 20) has500 = true;
        if (a.at == 1500 && a.pos == 80) has1500 = true;
        CHECK_FALSE((a.at == 1000 && a.pos == 99));
    }
    CHECK(has500);
    CHECK(has1500);
}

TEST_CASE("HealTrackingGaps does not densify outside the window") {
    const std::vector<Action> in{{0, 0}, {500, 80}, {1000, 20}, {1500, 90}, {5000, 10}, {5100, 50}};
    int healed = 0, added = 0;
    const auto out = samn::HealTrackingGaps(in, {{2000, 4500}}, 200, 0, &healed, &added);
    CHECK(healed >= 1);
    CHECK(added >= 1);
    int early = 0, outside = 0;
    for (const auto& a : out) {
        if (a.at > 0 && a.at < 1500) ++early;
        if ((a.at > 1500 && a.at < 2000) || (a.at > 4500 && a.at < 5000)) ++outside;
    }
    CHECK(early == 2);
    CHECK(outside == 0);
}

TEST_CASE("HealTrackingGaps merges overlapping and inverted windows") {
    const std::vector<Action> in{{0, 0}, {100, 10}, {800, 95}, {2000, 90}, {2100, 100}};
    int healed = 0;
    const auto out = samn::HealTrackingGaps(in, {{1500, 400}, {300, 900}}, 100, 0, &healed);
    CHECK(healed >= 1);
    for (const auto& a : out) CHECK_FALSE((a.at == 800 && a.pos == 95));
}

TEST_CASE("HealTrackingGaps refuses when it would destroy the script") {
    const std::vector<Action> in{{100, 10}, {200, 20}, {300, 30}};
    int healed = 0, added = 0;
    const auto out = samn::HealTrackingGaps(in, {{0, 400}}, 50, 0, &healed, &added);
    CHECK(healed == 0);
    CHECK(added == 0);
    CHECK(out.size() == in.size());
}

TEST_CASE("ImproveScript heals and keeps the heal points when Fill finds nothing") {
    const std::vector<Action> in{{0, 0}, {100, 10}, {800, 95}, {1200, 94}, {2000, 90}, {2100, 100}};
    samn::ImproveOptions heal;
    heal.heal_tracking_gaps = true;
    heal.tracking_gaps = {{400, 1600}};
    heal.step_ms = 200;
    const auto heal_only = samn::ImproveScript(in, heal);
    CHECK(heal_only.windows_healed >= 1);
    CHECK(heal_only.clear_tracking_gaps);
    CHECK(heal_only.points_added >= 1);
    auto both_opts = heal;
    both_opts.fill_gaps = true;
    const auto both = samn::ImproveScript(in, both_opts);
    CHECK(both.windows_healed >= 1);
    CHECK(both.gaps_filled == 0);
    CHECK(both.points_added >= heal_only.points_added);
    CHECK(both.after_count == heal_only.after_count);
}
