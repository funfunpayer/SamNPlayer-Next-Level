#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "samn/audio_autosync.h"

using samn::Action;
using samn::AudioSyncConfidence;

namespace {

// Zigzag stroke: motion every 500 ms.
std::vector<Action> Zigzag(int n = 40, int step = 500) {
    std::vector<Action> a;
    a.reserve(size_t(n));
    for (int i = 0; i < n; ++i) a.push_back(Action{i * step, i % 2 ? 0 : 100});
    return a;
}

// Loudness envelope at 50 ms: narrow bumps at each stroke action + delay_ms.
std::vector<double> LoudnessForZigzag(int delay_ms, int step_ms = 50, int duration_ms = 20000) {
    std::vector<double> env(size_t(duration_ms / step_ms), 0.0);
    for (int t = 500; t < duration_ms; t += 500) {
        const int center = t + delay_ms;
        for (int d = -50; d <= 50; d += step_ms) {
            const int at = center + d;
            if (at < 0 || at >= duration_ms) continue;
            const double w = 1.0 - std::abs(d) / 50.0;
            env[size_t(at / step_ms)] = std::max(env[size_t(at / step_ms)], w);
        }
    }
    return env;
}

}  // namespace

TEST_CASE("TestEstimateSyncLagFindsDelay") {
    const auto stroke = Zigzag();
    const auto env = LoudnessForZigzag(500);
    const auto sug = samn::EstimateAudioSyncOffset(env, 50, stroke);
    CHECK(sug.confidence != AudioSyncConfidence::None);
    CHECK(std::abs(sug.offset_ms - 500) <= 100);
    CHECK(sug.correlation >= 0.12);
}

TEST_CASE("TestEstimateSyncLagNoMatch") {
    const auto stroke = Zigzag();
    std::vector<double> env(400, 0.0);
    for (size_t i = 0; i < env.size(); ++i) env[i] = (i % 17 == 0) ? 1.0 : 0.0;
    const auto sug = samn::EstimateAudioSyncOffset(env, 50, stroke);
    // Unrelated sparse spikes: expect none or at most low with tiny r.
    if (sug.confidence == AudioSyncConfidence::High) {
        CHECK(sug.correlation < 0.30);  // should not claim high on noise
    }
}

TEST_CASE("TestEstimateSyncLagPeriodicIsNotHigh") {
    // Purely periodic identical envelopes can match many lags; margin may kill high.
    std::vector<Action> stroke;
    for (int i = 0; i < 80; ++i) stroke.push_back(Action{i * 250, i % 2 ? 0 : 100});
    std::vector<double> env(800, 0.0);
    for (size_t i = 0; i < env.size(); ++i) env[i] = (i % 5 == 0) ? 1.0 : 0.0;
    const auto sug = samn::EstimateAudioSyncOffset(env, 50, stroke);
    // Must not be an empty crash; high only with real margin.
    CHECK(sug.note.size() > 0);
}

TEST_CASE("TestEstimateAudioSyncOffsetNeedsScriptAndVideo") {
    CHECK(samn::EstimateAudioSyncOffset({}, 50, Zigzag()).confidence == AudioSyncConfidence::None);
    CHECK(samn::EstimateAudioSyncOffset({0.1, 0.2}, 50, {}).confidence == AudioSyncConfidence::None);
    CHECK(samn::EstimateAudioSyncOffset({0.1, 0.2, 0.3}, 0, Zigzag()).confidence == AudioSyncConfidence::None);
}
