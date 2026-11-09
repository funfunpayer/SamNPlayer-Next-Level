#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

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

synth::Spec Hits(double bpm) {
    synth::Spec s;
    s.bpm = bpm;
    return s;
}

}  // namespace

TEST_CASE("onsets: frame layout at 11025 Hz and other rates") {
    samn::OnsetCurve oc;
    CHECK(oc.FrameSize() == 512);
    CHECK(oc.Hop() == 256);
    CHECK(oc.FramesPerSecond() == doctest::Approx(43.066).epsilon(1e-3));
    CHECK(samn::OnsetCurve(44100).FrameSize() == 2048);
    CHECK(samn::OnsetCurve(8000).FrameSize() == 256);  // nearest to 46 ms (371 samples)
    CHECK(samn::OnsetCurve(1).SampleRate() == 4000);   // clamped
}

TEST_CASE("onsets: empty, short and silent input") {
    const auto empty = Analyse({});
    CHECK(empty.curve.empty());
    CHECK(empty.blocks.empty());
    CHECK(samn::EstimateTempo(empty.curve, empty.fps).bpm == 0);
    CHECK(samn::TrackBeats(empty.curve, empty.fps, 72).beats_ms.empty());
    CHECK_FALSE(samn::AssessTempo(empty.blocks, {}).reliable);

    // 100 samples: one centred frame, no block (less than half full).
    const auto shortest = Analyse(std::vector<float>(100, 0.5f));
    CHECK(shortest.curve.size() == 1);
    CHECK(shortest.blocks.empty());

    const auto silent = Analyse(std::vector<float>(11025 * 10, 0.0f));
    CHECK(silent.curve.size() == 431);  // (110250 - 1) / 256 + 1
    for (float v : silent.curve) CHECK(v == 0.0f);
    REQUIRE(silent.blocks.size() == 10);
    for (const auto& b : silent.blocks) CHECK(b.cls == samn::AudioClass::kSilence);
    const auto t = samn::EstimateTempo(silent.curve, silent.fps);
    CHECK(t.bpm == 0);
    CHECK(t.confidence == 0);
    CHECK(samn::TrackBeats(silent.curve, silent.fps, 72).beats_ms.empty());
    CHECK(samn::PickOnsets(silent.curve, silent.fps).empty());
    const auto rel = samn::AssessTempo(silent.blocks, t);
    CHECK_FALSE(rel.reliable);
    CHECK(std::string(rel.reason) == "mostly silence");
}

TEST_CASE("onsets: NaN, infinity and clipping never reach the outputs") {
    auto clip = synth::Make(Hits(90));
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < clip.samples.size(); i += 997) clip.samples[i] = (i / 997) % 3 == 0 ? nan : (i / 997) % 3 == 1 ? inf : 7.0f;
    const auto a = Analyse(clip.samples);
    for (float v : a.curve) {
        CHECK(std::isfinite(v));
        CHECK(v >= 0.0f);
    }
    for (const auto& b : a.blocks) {
        CHECK(std::isfinite(b.level_db));
        CHECK((b.flatness >= 0 && b.flatness <= 1));
        CHECK((b.quiet_share >= 0 && b.quiet_share <= 1));
    }
    const auto t = samn::EstimateTempo(a.curve, a.fps);
    CHECK(std::isfinite(t.bpm));
    CHECK(std::isfinite(t.confidence));
    // Garbage in the curve itself.
    std::vector<float> bad = a.curve;
    bad[10] = nan;
    bad[20] = -inf;
    bad[30] = inf;
    const auto t2 = samn::EstimateTempo(bad, a.fps);
    CHECK(std::isfinite(t2.bpm));
    const auto beats = samn::TrackBeats(bad, a.fps, 90);
    for (double b : beats.beats_ms) CHECK(std::isfinite(b));
    CHECK(samn::EstimateTempo(a.curve, std::numeric_limits<double>::quiet_NaN()).bpm == 0);
    CHECK(samn::TrackBeats(a.curve, a.fps, std::numeric_limits<double>::quiet_NaN()).beats_ms.empty());
}

TEST_CASE("onsets: chunk size does not change a single bit, and runs repeat") {
    const auto clip = synth::Make(Hits(110));
    const auto whole = Analyse(clip.samples);
    samn::OnsetCurve oc;
    synth::Rng rng(7);
    std::vector<float> drained;
    std::size_t i = 0;
    while (i < clip.samples.size()) {
        const std::size_t n = std::min<std::size_t>(clip.samples.size() - i, 1 + rng.Next() % 3000);
        oc.Push(clip.samples.data() + i, n);
        i += n;
        const auto part = oc.TakeValues();
        drained.insert(drained.end(), part.begin(), part.end());
    }
    oc.Finish();
    const auto tail = oc.TakeValues();
    drained.insert(drained.end(), tail.begin(), tail.end());
    REQUIRE(drained.size() == whole.curve.size());
    for (std::size_t k = 0; k < drained.size(); ++k) CHECK(drained[k] == whole.curve[k]);
    CHECK(oc.Frames() == whole.curve.size());
    const auto again = Analyse(clip.samples);
    CHECK(again.curve == whole.curve);
    const auto t1 = samn::EstimateTempo(whole.curve, whole.fps), t2 = samn::EstimateTempo(again.curve, again.fps);
    CHECK(t1.bpm == t2.bpm);
    CHECK(t1.confidence == t2.confidence);
    CHECK(samn::TrackBeats(whole.curve, whole.fps, t1.bpm).beats_ms ==
          samn::TrackBeats(again.curve, again.fps, t2.bpm).beats_ms);
}

TEST_CASE("onsets: drum hits are found within 50 ms") {
    for (double bpm : {60.0, 95.0, 150.0}) {
        CAPTURE(bpm);
        const auto clip = synth::Make(Hits(bpm));
        const auto a = Analyse(clip.samples);
        const auto found = samn::PickOnsets(a.curve, a.fps);
        CHECK(synth::FMeasure(clip.onsets_ms, found, 50) >= 0.95);
    }
    // A steady chord has no onsets.
    synth::Spec pad;
    pad.hits = false;
    const auto a = Analyse(synth::Make(pad).samples);
    CHECK(samn::PickOnsets(a.curve, a.fps).size() <= 1);
}

TEST_CASE("onsets: the minimum gap holds at every frame rate") {
    // 4 kHz and 96 kHz give 62.5 and 46.875 values per second: 70 ms is
    // 4.4 and 3.3 values, which must round up, not to the nearest.
    for (const int rate : {4000, 11025, 44100, 96000}) {
        CAPTURE(rate);
        const double fps = samn::OnsetCurve(rate).FramesPerSecond();
        std::vector<float> curve(600, 0.0f);
        for (std::size_t i = 0; i < curve.size(); i += 3) curve[i] = 10.0f + float(i % 7);
        const auto on = samn::PickOnsets(curve, fps);
        REQUIRE(on.size() >= 10);
        for (std::size_t k = 1; k < on.size(); ++k) CHECK(on[k] - on[k - 1] >= 70.0 - 1e-9);
    }
}

TEST_CASE("tempo: Estimate leaves the accumulator unchanged and repeats") {
    const auto a = Analyse(synth::Make(Hits(90)).samples);
    samn::TempoAccumulator acc(a.fps);
    acc.Push(a.curve.data(), a.curve.size());
    const samn::TempoAccumulator& view = acc;
    const auto t1 = view.Estimate(), t2 = view.Estimate();
    CHECK(t1.bpm == t2.bpm);
    CHECK(t1.confidence == t2.confidence);
    CHECK(t1.raw_bpm == t2.raw_bpm);
}

TEST_CASE("tempo: known click tempi, the heart prior and octave folding") {
    for (double bpm : {50.0, 60.0, 72.0, 85.0, 100.0, 120.0}) {
        CAPTURE(bpm);
        auto spec = Hits(bpm);
        spec.jitter_ms = 10;
        const auto a = Analyse(synth::Make(spec).samples);
        const auto t = samn::EstimateTempo(a.curve, a.fps);
        CHECK(synth::TempoWithin(t.bpm, bpm));
        CHECK(t.confidence >= 0.5);
    }
    // Fast tempi fold to the octave nearest the 72 BPM prior.
    for (double bpm : {150.0, 180.0}) {
        CAPTURE(bpm);
        const auto a = Analyse(synth::Make(Hits(bpm)).samples);
        const auto t = samn::EstimateTempo(a.curve, a.fps);
        CHECK(synth::TempoWithin(t.bpm, bpm / 2));
        CHECK(synth::TempoWithin(t.raw_bpm, bpm));
    }
    // A prior elsewhere picks the other octave.
    const auto a = Analyse(synth::Make(Hits(180)).samples);
    samn::TempoOptions fast;
    fast.prior_bpm = 160;
    CHECK(synth::TempoWithin(samn::EstimateTempo(a.curve, a.fps, fast).bpm, 180));
}

TEST_CASE("tempo: off-beat hats, 16th hats and bass do not give 2/5, 4/5 or 1/3 of the tempo") {
    // Regression (review of D-168): clicks with noise hats on the off-beat
    // locked to 0.4 x the tempo from about 100 BPM; 16th hats gave 0.8 x
    // and 1/3. Octave errors (x1/2, x2) are allowed: the prior folds.
    const auto check = [](synth::Spec s) {
        s.seed = std::uint64_t(s.bpm) * 7 + 1;
        s.jitter_ms = 5;
        const auto a = Analyse(synth::Make(s).samples);
        return samn::EstimateTempo(a.curve, a.fps).bpm;
    };
    for (double bpm : {99.0, 106.0, 120.0, 148.0}) {
        CAPTURE(bpm);
        synth::Spec s;
        s.bpm = bpm;
        s.click_kick = true;
        s.pad = 0;
        s.offbeat_hats = true;
        s.hat_level = 0.4;
        s.hat_decay_ms = 30;
        const double got = check(s);
        CAPTURE(got);
        CHECK(synth::TempoOctaveWithin(got, bpm));
    }
    for (double bpm : {78.0, 155.0, 176.0}) {
        CAPTURE(bpm);
        synth::Spec s;
        s.bpm = bpm;
        s.click_kick = true;
        s.pad = 0;
        s.hat_every = 4;
        s.hat_level = 0.3;
        s.hat_decay_ms = 30;
        const double got = check(s);
        CAPTURE(got);
        CHECK(synth::TempoOctaveWithin(got, bpm));
    }
    for (double bpm : {141.0, 155.0, 176.0}) {
        CAPTURE(bpm);
        synth::Spec s;
        s.bpm = bpm;
        s.sub_bass = 0.9;
        s.hit = 0.4;
        s.hat_every = 2;
        s.hat_level = 0.2;
        const double got = check(s);
        CAPTURE(got);
        CHECK(synth::TempoOctaveWithin(got, bpm));
    }
}

TEST_CASE("tempo: under 4 s of curve says nothing; the state is fixed") {
    const auto a = Analyse(synth::Make(Hits(90)).samples);
    const auto few = std::size_t(3.5 * a.fps);
    CHECK(samn::EstimateTempo(a.curve.data(), few, a.fps).bpm == 0);
    samn::TempoAccumulator acc(a.fps);
    const auto bytes = acc.StateBytes();
    CHECK(bytes < 8 * 1024);
    acc.Push(a.curve.data(), a.curve.size());
    CHECK(acc.StateBytes() == bytes);
    CHECK(acc.Count() == a.curve.size());
}

TEST_CASE("beats: Ellis DP follows jittered hits, and is empty without a pulse") {
    auto spec = Hits(80);
    spec.jitter_ms = 15;
    const auto clip = synth::Make(spec);
    const auto a = Analyse(clip.samples);
    const auto t = samn::EstimateTempo(a.curve, a.fps);
    const auto beats = samn::TrackBeats(a.curve, a.fps, t.bpm);
    CHECK(synth::FMeasure(clip.beats_ms, beats.beats_ms, 70) >= 0.95);
    CHECK(beats.confidence >= 0.8);
    for (std::size_t k = 1; k < beats.beats_ms.size(); ++k) CHECK(beats.beats_ms[k] > beats.beats_ms[k - 1]);
    // A constant curve has nothing to follow.
    CHECK(samn::TrackBeats(std::vector<float>(2000, 1.0f), a.fps, 80).beats_ms.empty());
}

TEST_CASE("gate: silence, steady noise, speech-like bursts and music") {
    synth::Spec noise;
    noise.hits = false;
    noise.pad = 0;
    noise.noise = 0.2;
    const auto n = Analyse(synth::Make(noise).samples);
    for (const auto& b : n.blocks) CHECK(b.cls == samn::AudioClass::kNoise);
    const auto rn = samn::AssessTempo(n.blocks, samn::EstimateTempo(n.curve, n.fps));
    CHECK_FALSE(rn.reliable);

    synth::Spec speech;
    speech.hits = false;
    speech.pad = 0;
    speech.speech = true;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        CAPTURE(seed);
        speech.seed = seed;
        const auto sp = Analyse(synth::Make(speech).samples);
        const auto rs = samn::AssessTempo(sp.blocks, samn::EstimateTempo(sp.curve, sp.fps));
        CHECK(rs.speech_share >= 0.8);
        CHECK_FALSE(rs.reliable);
    }

    const auto m = Analyse(synth::Make(Hits(90)).samples);
    const auto rm = samn::AssessTempo(m.blocks, samn::EstimateTempo(m.curve, m.fps));
    CHECK(rm.music_share >= 0.8);
    CHECK(rm.reliable);
    CHECK(std::string(rm.reason).empty());

    // Sparse hits without a pad are bursty, but the pulse is clear.
    auto sparse = Hits(90);
    sparse.pad = 0;
    const auto s = Analyse(synth::Make(sparse).samples);
    const auto rsp = samn::AssessTempo(s.blocks, samn::EstimateTempo(s.curve, s.fps));
    CHECK(rsp.speech_share >= 0.8);
    CHECK(rsp.reliable);
    CHECK(std::string(samn::AudioClassName(samn::AudioClass::kSpeechLike)) == "speech-like");
}

TEST_CASE("onsets: 10 hours at 11 kHz stream in a fixed state") {
    // One drum-like click per second, drained after every chunk as a
    // streaming caller would; the tempo accumulator takes the whole curve.
    samn::OnsetCurve oc;
    samn::TempoAccumulator acc(oc.FramesPerSecond());
    const std::size_t state = oc.StateBytes(), tempo_state = acc.StateBytes();
    CHECK(state < 16 * 1024);
    CHECK(tempo_state < 8 * 1024);
    std::vector<float> second(samn::kOnsetSampleRate, 0.0f);
    for (int i = 0; i < 40; ++i) second[std::size_t(i)] = (i % 2 ? -0.5f : 0.5f) * float(40 - i) / 40.0f;
    std::size_t max_values = 0, blocks = 0;
    constexpr int kSeconds = 10 * 3600;
    for (int s = 0; s < kSeconds; ++s) {
        oc.Push(second.data(), second.size());
        const auto v = oc.TakeValues();
        max_values = std::max(max_values, v.capacity());
        acc.Push(v.data(), v.size());
        blocks += oc.TakeBlocks().size();
    }
    oc.Finish();
    const auto tail = oc.TakeValues();
    acc.Push(tail.data(), tail.size());
    blocks += oc.TakeBlocks().size();
    CHECK(oc.StateBytes() == state);
    CHECK(acc.StateBytes() == tempo_state);
    CHECK(max_values <= 64);  // one second of values, never the whole track
    CHECK(oc.Frames() == (std::uint64_t(kSeconds) * samn::kOnsetSampleRate - 1) / 256 + 1);
    CHECK(blocks >= std::size_t(kSeconds) - 1);
    const auto t = acc.Estimate();
    CHECK(synth::TempoOctaveWithin(t.bpm, 60));
    CHECK(std::isfinite(t.confidence));
}
