#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "samn/audio_envelope.h"
#include "samn/audio_onsets.h"
#include "samn/audio_tempo.h"
#include "synthetic_audio.h"

namespace {

struct Analysis {
    std::vector<float> curve;
    std::vector<samn::AudioBlock> blocks;
    double fps = 0;
};

Analysis Analyse(const std::vector<float>& samples, int sample_rate = samn::kOnsetSampleRate) {
    samn::OnsetCurve oc(sample_rate);
    oc.Push(samples.data(), samples.size());
    oc.Finish();
    return {oc.Values(), oc.Blocks(), oc.FramesPerSecond()};
}

std::vector<double> LoudnessBeats(double bpm, int step_ms = 50, int seconds = 20) {
    const int n = seconds * 1000 / step_ms;
    const int period = int(std::lround(60000.0 / bpm / step_ms));
    std::vector<double> env(std::size_t(n), 0.05);
    for (int i = period; i + 1 < n; i += period) env[std::size_t(i)] = 0.9;
    return env;
}

}  // namespace

TEST_CASE("playback BPM: high-confidence onsets win over a conflicting loudness beat") {
    samn::TempoEstimate tempo;
    tempo.bpm = 90;
    tempo.confidence = 0.80;
    tempo.raw_bpm = 90;
    samn::TempoReliability rel;
    rel.reliable = true;
    const auto loud = LoudnessBeats(120);
    REQUIRE(samn::EstimateBpmFromLoudness(loud) == 120);

    const auto g = samn::PickPlaybackAudioBpm(tempo, rel, loud);
    CHECK(g.source == samn::PlaybackBpmSource::kOnsets);
    CHECK(g.bpm == 90);
    CHECK(g.confidence == doctest::Approx(0.80));
    CHECK(std::string(g.reason).empty());
}

TEST_CASE("playback BPM: silence and a weak pulse fall back to loudness") {
    const auto loud = LoudnessBeats(120);
    REQUIRE(samn::EstimateBpmFromLoudness(loud) == 120);

    samn::TempoEstimate silent_tempo;
    silent_tempo.bpm = 72;
    silent_tempo.confidence = 0.90;
    samn::TempoReliability silent;
    silent.reliable = false;
    silent.reason = "mostly silence";
    const auto gs = samn::PickPlaybackAudioBpm(silent_tempo, silent, loud);
    CHECK(gs.source == samn::PlaybackBpmSource::kLoudness);
    CHECK(gs.bpm == 120);
    CHECK(std::string(gs.reason) == "mostly silence");

    samn::TempoEstimate weak_tempo;
    weak_tempo.bpm = 100;
    weak_tempo.confidence = 0.10;
    samn::TempoReliability weak;
    weak.reliable = false;
    weak.reason = "weak pulse";
    const auto gw = samn::PickPlaybackAudioBpm(weak_tempo, weak, loud);
    CHECK(gw.source == samn::PlaybackBpmSource::kLoudness);
    CHECK(gw.bpm == 120);
    CHECK(std::string(gw.reason) == "weak pulse");
    CHECK(gw.confidence == doctest::Approx(0.10));
}

TEST_CASE("playback BPM: both failing leaves no guess") {
    samn::TempoReliability none;
    none.reason = "no sound analysed";
    const auto g = samn::PickPlaybackAudioBpm({}, none, std::vector<double>(20, 0.1));
    CHECK(g.source == samn::PlaybackBpmSource::kNone);
    CHECK(g.bpm == 0);
    CHECK(std::string(g.reason) == "no sound analysed");
}

TEST_CASE("playback BPM: EstimateTempo / AssessTempo on music, silence and speech") {
    const auto loud = LoudnessBeats(120);
    REQUIRE(samn::EstimateBpmFromLoudness(loud) == 120);

    const auto music = Analyse(synth::Make(synth::Spec{}).samples);
    const auto tm = samn::EstimateTempo(music.curve, music.fps);
    const auto rm = samn::AssessTempo(music.blocks, tm);
    REQUIRE(rm.reliable);
    const auto gm = samn::PickPlaybackAudioBpm(tm, rm, loud);
    CHECK(gm.source == samn::PlaybackBpmSource::kOnsets);
    CHECK(synth::TempoWithin(gm.bpm, 90));

    const auto silent = Analyse(std::vector<float>(11025 * 10, 0.0f));
    const auto ts = samn::EstimateTempo(silent.curve, silent.fps);
    const auto rs = samn::AssessTempo(silent.blocks, ts);
    REQUIRE_FALSE(rs.reliable);
    CHECK(std::string(rs.reason) == "mostly silence");
    const auto gs = samn::PickPlaybackAudioBpm(ts, rs, loud);
    CHECK(gs.source == samn::PlaybackBpmSource::kLoudness);
    CHECK(gs.bpm == 120);

    synth::Spec speech;
    speech.hits = false;
    speech.pad = 0;
    speech.speech = true;
    speech.seed = 1;
    const auto sp = Analyse(synth::Make(speech).samples);
    const auto tp = samn::EstimateTempo(sp.curve, sp.fps);
    const auto rp = samn::AssessTempo(sp.blocks, tp);
    REQUIRE_FALSE(rp.reliable);
    const auto gp = samn::PickPlaybackAudioBpm(tp, rp, loud);
    CHECK(gp.source == samn::PlaybackBpmSource::kLoudness);
    CHECK(gp.bpm == 120);
}
