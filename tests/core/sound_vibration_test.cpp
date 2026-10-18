#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "samn/samn_file.h"
#include "samn/sound_vibration.h"

using samn::Action;
using samn::Frame;

// Go: TestApplyAudioVibrationKeepsSuctionAndShiftsByOffset
TEST_CASE("TestApplyAudioVibrationKeepsSuctionAndShiftsByOffset") {
    const std::vector<Frame> frames{{0, 0.9, 0.1}, {50, 0.9, 0.2}, {100, 0.9, 0.3}};
    const std::vector<double> env{0, 0.5, 1, 0.25, 0, 0};
    const auto out = samn::ApplyAudioVibration(frames, env, 50, 0, 1);
    const double want[] = {0, 0.5, 1};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(out[i].vibration == want[i]);
        CHECK(out[i].suction == frames[i].suction);
    }
    const auto shifted = samn::ApplyAudioVibration(frames, env, 50, 50, 1);
    CHECK(shifted[0].vibration == 0.5);
    CHECK(shifted[2].vibration == 0.25);
    CHECK(frames[0].vibration == 0.9);
}

// Go: TestApplyAudioVibrationExtendsToAudioEnd
TEST_CASE("TestApplyAudioVibrationExtendsToAudioEnd") {
    const std::vector<Frame> frames{{0, 0, 0.4}, {50, 0, 0.4}};
    const std::vector<double> env{0, 0, 0, 0.8, 0};
    const auto out = samn::ApplyAudioVibration(frames, env, 50, 0, 0.5);
    REQUIRE(out.size() == 5);
    CHECK(out[4].at == 200);
    CHECK(out[3].vibration == doctest::Approx(0.4));
    CHECK(out[3].suction == 0);
}

// Go: TestApplyAudioVibrationNoEnvelopeIsNoop
TEST_CASE("TestApplyAudioVibrationNoEnvelopeIsNoop") {
    const std::vector<Frame> frames{{0, 0.3, 0}};
    CHECK(samn::ApplyAudioVibration(frames, {}, 50, 0, 1)[0].vibration == 0.3);
}

TEST_CASE("FramesToLane samples every 100 ms and folds equal runs") {
    std::vector<Frame> frames;
    for (int64_t t = 0; t <= 1000; t += 25) frames.push_back(Frame{t, t < 500 ? 0.5 : 1.0, 0.2});
    const auto lane = samn::FramesToLane(frames, true);
    // 0.5 from 0 to 400 (first and last kept), 1.0 from 500 to 1000.
    REQUIRE(lane.size() == 4);
    CHECK(lane[0].at == 0);
    CHECK(lane[0].pos == 50);
    CHECK(lane[1].at == 400);
    CHECK(lane[2].at == 500);
    CHECK(lane[2].pos == 100);
    CHECK(lane[3].at == 1000);
    const auto suction = samn::FramesToLane(frames, false);
    REQUIRE(suction.size() == 2);
    CHECK(suction[0].pos == 20);
    CHECK(suction[1].at == 1000);
}

namespace {

// 0.7 soundVibApp: 21 strokes, 250 ms apart, 10/90.
std::vector<Frame> Mapped() {
    samn::FunscriptView s;
    for (int i = 0; i <= 20; ++i) s.actions.push_back(Action{i * 250, i % 2 ? 90 : 10});
    return *samn::ToIntensityCurve(s.actions, samn::MapOptionsFromScript(s));
}

int At(const std::vector<Action>& lane, int64_t ms) {
    Action best = lane.front();
    for (const Action& p : lane)
        if (p.at <= ms) best = p;
    return best.pos;
}

}  // namespace

// Go: TestBakeSoundVibrationFollowsLoudness
TEST_CASE("TestBakeSoundVibrationFollowsLoudness") {
    std::vector<double> env(100, 0);  // 5 s at 50 ms
    std::fill(env.begin(), env.begin() + 20, 1.0);
    const auto lane = samn::SoundVibrationLane(Mapped(), env, 0, samn::SoundVibrationMode::kSound);
    REQUIRE(!lane.empty());
    CHECK(At(lane, 500) >= 90);
    CHECK(At(lane, 3000) <= 10);
    CHECK(lane.back().at >= 4900);  // follows the sound past the last stroke
}

// Go: TestBakeSoundVibrationWithContactKeepsScriptVibration
TEST_CASE("TestBakeSoundVibrationWithContactKeepsScriptVibration") {
    const std::vector<double> env(100, 0);  // silence
    const auto lane = samn::SoundVibrationLane(Mapped(), env, 0, samn::SoundVibrationMode::kSoundContact);
    int max = 0;
    for (const Action& p : lane) max = std::max(max, p.pos);
    CHECK(max > 0);
    const auto soundOnly = samn::SoundVibrationLane(Mapped(), env, 0, samn::SoundVibrationMode::kSound);
    for (const Action& p : soundOnly) CHECK(p.pos == 0);
    CHECK(samn::SoundVibrationLane({}, env, 0, samn::SoundVibrationMode::kSound).empty());
}
