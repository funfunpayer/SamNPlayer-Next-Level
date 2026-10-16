#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "samn/mapper.h"

using samn::Action;
using samn::Frame;
using samn::SyncMode;

namespace {
double MaxVib(const std::vector<Frame>& f) {
    double m = f.front().vibration;
    for (const auto& x : f) m = std::max(m, x.vibration);
    return m;
}
}  // namespace

TEST_CASE("mapper: standstill") {
    std::vector<Action> actions{{0, 50}, {1000, 50}, {2000, 50}};
    auto opts = samn::DefaultMapOptions();
    opts.min_vibration = 0;
    opts.smoothing = 0;
    auto frames = samn::ToIntensityCurve(actions, opts);
    REQUIRE(frames.has_value());
    REQUIRE(!frames->empty());
    for (const auto& f : *frames) CHECK(f.vibration <= 0.001);
}

TEST_CASE("mapper: fast stroke reaches full") {
    std::vector<Action> actions{{0, 0}, {150, 100}, {300, 0}};
    auto opts = samn::DefaultMapOptions();
    opts.smoothing = 0;
    auto frames = samn::ToIntensityCurve(actions, opts);
    REQUIRE(frames.has_value());
    CHECK(MaxVib(*frames) >= 0.99);
}

TEST_CASE("mapper: floor preserves gradations") {
    auto opts = samn::DefaultMapOptions();
    opts.min_vibration = 0.15;
    opts.smoothing = 0;
    opts.sync = SyncMode::VibrationOnly;

    auto intensity = [&](int delta_pos, int64_t delta_ms) {
        auto frames = samn::ToIntensityCurve({{0, 0}, {delta_ms, delta_pos}, {delta_ms * 2, 0}}, opts);
        REQUIRE(frames.has_value());
        REQUIRE(!frames->empty());
        return frames->front().vibration;
    };
    double still = intensity(0, 200);
    double slow = intensity(12, 200);
    double medium = intensity(24, 200);

    CHECK(std::fabs(still - 0.15) <= 0.01);
    CHECK(std::fabs(slow - 0.235) <= 0.02);
    CHECK(std::fabs(medium - 0.32) <= 0.02);
    CHECK(slow > still + 0.01);
    CHECK(medium > slow + 0.01);
}

TEST_CASE("mapper: floor keeps ceiling") {
    auto opts = samn::DefaultMapOptions();
    opts.min_vibration = 0.4;
    opts.smoothing = 0;
    opts.sync = SyncMode::VibrationOnly;
    auto frames = samn::ToIntensityCurve({{0, 0}, {100, 100}, {200, 0}}, opts);
    REQUIRE(frames.has_value());
    for (const auto& f : *frames) CHECK(f.vibration <= 1.0001);
}

TEST_CASE("mapper: floor respects channel modes") {
    std::vector<Action> actions{{0, 0}, {200, 100}, {400, 0}};
    auto opts = samn::DefaultMapOptions();
    opts.min_vibration = 0.3;
    opts.min_suction = 0.3;
    opts.smoothing = 0;

    opts.sync = SyncMode::VibrationOnly;
    auto a = samn::ToIntensityCurve(actions, opts);
    REQUIRE(a.has_value());
    for (const auto& f : *a) CHECK(f.suction <= 0.001);

    opts.sync = SyncMode::SuctionOnly;
    auto b = samn::ToIntensityCurve(actions, opts);
    REQUIRE(b.has_value());
    for (const auto& f : *b) CHECK(f.vibration <= 0.001);
}

TEST_CASE("mapper: independent uses position for suction") {
    std::vector<Action> actions{{0, 0}, {2000, 100}};
    auto opts = samn::DefaultMapOptions();
    opts.smoothing = 0;
    opts.min_vibration = 0;
    opts.min_suction = 0;
    opts.sync = SyncMode::Independent;

    auto frames = samn::ToIntensityCurve(actions, opts);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() >= 10);
    const Frame& first = frames->front();
    const Frame& last = (*frames)[frames->size() - 2];
    CHECK(std::fabs(first.vibration - last.vibration) <= 0.05);
    CHECK(last.suction > first.suction + 0.5);
}

TEST_CASE("mapper: smoothing stays in range") {
    std::vector<Action> actions{{0, 0}, {100, 100}, {200, 0}, {300, 100}};
    auto opts = samn::DefaultMapOptions();
    opts.smoothing = 0.8;
    auto sm = samn::ToIntensityCurve(actions, opts);
    REQUIRE(sm.has_value());
    for (const auto& f : *sm) {
        CHECK((0 <= f.vibration && f.vibration <= 1));
        CHECK((0 <= f.suction && f.suction <= 1));
    }

    opts.smoothing = 0;
    double raw_max = MaxVib(*samn::ToIntensityCurve(actions, opts));
    opts.smoothing = 0.8;
    double smooth_max = MaxVib(*samn::ToIntensityCurve(actions, opts));
    CHECK(smooth_max <= raw_max + 0.001);
}

TEST_CASE("mapper: edge cases") {
    auto opts = samn::DefaultMapOptions();
    CHECK(!samn::ToIntensityCurve({}, opts).has_value());
    CHECK(!samn::ToIntensityCurve({{0, 50}}, opts).has_value());

    auto dup = samn::ToIntensityCurve({{0, 0}, {0, 100}, {500, 0}}, opts);
    REQUIRE(dup.has_value());
    for (const auto& f : *dup) CHECK(!(std::isnan(f.vibration) || std::isinf(f.vibration)));

    auto broken = samn::DefaultMapOptions();
    broken.tick_ms = 0;
    broken.max_speed = 0;
    auto frames = samn::ToIntensityCurve({{0, 0}, {1000, 100}}, broken);
    REQUIRE(frames.has_value());
    CHECK(!frames->empty());
    for (const auto& f : *frames) CHECK(!std::isnan(f.vibration));
}

TEST_CASE("mapper: frame timeline") {
    std::vector<Action> actions{{0, 0}, {1000, 100}, {2000, 0}};
    auto opts = samn::DefaultMapOptions();
    opts.tick_ms = 50;
    auto frames = samn::ToIntensityCurve(actions, opts);
    REQUIRE(frames.has_value());
    REQUIRE(frames->size() >= 40);
    CHECK((*frames)[0].at == 0);
    for (size_t i = 1; i < frames->size(); ++i) CHECK((*frames)[i].at > (*frames)[i - 1].at);
    CHECK(frames->back().at >= 1950);
}
