// Onsets, tempo and beats on synthetic sound with a known truth, and the
// heart-rhythm gap fill on synthetic stroke scripts (D-168,
// docs/benchmarks/AUDIO_ONSETS_2026-11-08.md). Deterministic (seeded); no
// recorded media.
//
// Before = what the code had: the Player's "BPM from audio"
// (EstimateBpmFromLoudness over the 50 ms loudness envelope) and its
// loudness peaks as onsets, plus a trivial baseline (the strongest
// autocorrelation lag of the same envelope, no prior). After = OnsetCurve,
// EstimateTempo, TrackBeats and AssessTempo.
//
//   audio_onset_bench               the tables (by hand: CPU times depend on the machine)
//   audio_onset_bench --memory-10h  10 hours streamed at 11025 Hz; exit 1 when the
//                                   analysis holds more than kMemoryBound bytes of heap
//                                   (run by ctest)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#define SAMN_HAVE_RUSAGE 1
#endif

#include "../core/synthetic_audio.h"
#include "samn/audio_envelope.h"
#include "samn/audio_onsets.h"
#include "samn/audio_tempo.h"
#include "samn/base_rhythm.h"
#include "samn/improve.h"
#include "samn/motion_fidelity.h"
#include "samn/script_quality.h"

// ---- Heap accounting: every allocation of this process -------------------

namespace {
std::atomic<long long> g_live{0}, g_peak{0};
void Note(long long delta) {
    const long long now = g_live.fetch_add(delta) + delta;
    long long peak = g_peak.load();
    while (now > peak && !g_peak.compare_exchange_weak(peak, now)) {
    }
}
// The size travels in front of the block (aligned to max_align_t).
constexpr std::size_t kHeader = alignof(std::max_align_t) > sizeof(std::size_t) ? alignof(std::max_align_t) : sizeof(std::size_t);
void* Alloc(std::size_t n) {
    void* p = std::malloc(n + kHeader);
    if (!p) throw std::bad_alloc();
    *static_cast<std::size_t*>(p) = n;
    Note((long long)n);
    return static_cast<char*>(p) + kHeader;
}
void Free(void* p) noexcept {
    if (!p) return;
    char* base = static_cast<char*>(p) - kHeader;
    Note(-(long long)*reinterpret_cast<std::size_t*>(base));
    std::free(base);
}
long long ResetPeak() {
    g_peak.store(g_live.load());
    return g_live.load();
}
}  // namespace

void* operator new(std::size_t n) { return Alloc(n); }
void* operator new[](std::size_t n) { return Alloc(n); }
void operator delete(void* p) noexcept { Free(p); }
void operator delete[](void* p) noexcept { Free(p); }
void operator delete(void* p, std::size_t) noexcept { Free(p); }
void operator delete[](void* p, std::size_t) noexcept { Free(p); }

namespace {

using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

long PeakRssKb() {
#ifdef SAMN_HAVE_RUSAGE
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
#if defined(__APPLE__)
    return long(u.ru_maxrss / 1024);
#else
    return long(u.ru_maxrss);
#endif
#else
    return -1;
#endif
}

// The analysis of a 10-hour stream may hold at most this much heap.
constexpr long long kMemoryBound = 32 * 1024;

// ---- Before: the loudness envelope the code had ---------------------------

struct Before {
    double bpm_existing = 0;   // EstimateBpmFromLoudness
    double bpm_trivial = 0;    // strongest autocorrelation lag of the envelope
    std::vector<double> onsets_ms;
    std::vector<double> beats_ms;  // a metronome at bpm_existing from the first peak
};

Before RunBefore(const std::vector<float>& samples, int sr, double seconds) {
    Before b;
    samn::WindowRms w(std::size_t(sr * samn::kAudioLevelStepMs / 1000));
    w.Push(samples.data(), samples.size());
    const auto loud = samn::NormalizeLoudness(w.Finish());
    b.bpm_existing = samn::EstimateBpmFromLoudness(loud);
    const double step = samn::kAudioLevelStepMs;
    for (std::size_t i = 1; i + 1 < loud.size(); ++i)
        if (loud[i] >= 0.35 && loud[i] >= loud[i - 1] && loud[i] > loud[i + 1]) b.onsets_ms.push_back(double(i) * step);
    // Trivial: argmax of the autocorrelation (mean removed) over 40-240 BPM.
    double mean = 0;
    for (double v : loud) mean += v;
    mean /= double(std::max<std::size_t>(1, loud.size()));
    double best = 0;
    for (int lag = int(std::ceil(60000.0 / 240 / step)); lag <= int(60000.0 / 40 / step); ++lag) {
        double s = 0;
        for (std::size_t i = std::size_t(lag); i < loud.size(); ++i) s += (loud[i] - mean) * (loud[i - std::size_t(lag)] - mean);
        s /= double(loud.size());
        if (s > best) {
            best = s;
            b.bpm_trivial = 60000.0 / (lag * step);
        }
    }
    if (b.bpm_existing > 0 && !b.onsets_ms.empty())
        for (double t = b.onsets_ms.front(); t < seconds * 1000; t += 60000.0 / b.bpm_existing) b.beats_ms.push_back(t);
    return b;
}

// ---- After -----------------------------------------------------------------

struct After {
    std::vector<float> curve;
    std::vector<samn::AudioBlock> blocks;
    double fps = 0;
    samn::TempoEstimate tempo;
    samn::BeatTrack beats;
    std::vector<double> onsets_ms;
    samn::TempoReliability reliability;
};

After RunAfter(const std::vector<float>& samples, int sr) {
    After a;
    samn::OnsetCurve oc(sr);
    oc.Push(samples.data(), samples.size());
    oc.Finish();
    a.curve = oc.Values();
    a.blocks = oc.Blocks();
    a.fps = oc.FramesPerSecond();
    a.onsets_ms = samn::PickOnsets(a.curve, a.fps);
    a.tempo = samn::EstimateTempo(a.curve, a.fps);
    a.beats = samn::TrackBeats(a.curve, a.fps, a.tempo.bpm);
    a.reliability = samn::AssessTempo(a.blocks, a.tempo);
    return a;
}

// ---- Families --------------------------------------------------------------

struct Row {
    std::string name;
    int clips = 0, tempo_clips = 0;
    // strict (within 4 %) and octave-corrected (x1/2, x1, x2) tempo hits
    int ex_strict = 0, ex_oct = 0, tr_strict = 0, tr_oct = 0, af_strict = 0, af_oct = 0;
    // strict hits where the truth is in the heart band (<= 130 BPM)
    int band_clips = 0, ex_band = 0, af_band = 0;
    double on_before = 0, on_after = 0, beat_before = 0, beat_after = 0;
    int reliable = 0;
};

void Score(Row& r, const synth::Clip& c, bool has_tempo) {
    const Before b = RunBefore(c.samples, c.spec.sample_rate, c.spec.seconds);
    const After a = RunAfter(c.samples, c.spec.sample_rate);
    ++r.clips;
    if (has_tempo) {
        const double truth = c.spec.bpm;
        ++r.tempo_clips;
        r.ex_strict += synth::TempoWithin(b.bpm_existing, truth);
        r.ex_oct += synth::TempoOctaveWithin(b.bpm_existing, truth);
        r.tr_strict += synth::TempoWithin(b.bpm_trivial, truth);
        r.tr_oct += synth::TempoOctaveWithin(b.bpm_trivial, truth);
        r.af_strict += synth::TempoWithin(a.tempo.bpm, truth);
        r.af_oct += synth::TempoOctaveWithin(a.tempo.bpm, truth);
        if (truth <= 130) {
            ++r.band_clips;
            r.ex_band += synth::TempoWithin(b.bpm_existing, truth);
            r.af_band += synth::TempoWithin(a.tempo.bpm, truth);
        }
        r.beat_before += synth::FMeasure(c.beats_ms, b.beats_ms, 70);
        r.beat_after += synth::FMeasure(c.beats_ms, a.beats.beats_ms, 70);
    }
    r.on_before += synth::FMeasure(c.onsets_ms, b.onsets_ms, 50);
    r.on_after += synth::FMeasure(c.onsets_ms, a.onsets_ms, 50);
    r.reliable += a.reliability.reliable;
}

void PrintRow(const Row& r) {
    const auto pct = [](int k, int n) { return n > 0 ? 100.0 * k / n : 0.0; };
    const double n = std::max(1, r.clips), nt = std::max(1, r.tempo_clips);
    if (r.tempo_clips > 0) {
        std::printf("| %-22s | %3d | %5.1f / %5.1f | %5.1f / %5.1f | %5.1f / %5.1f | %5.1f -> %5.1f | %.3f -> %.3f | %.3f -> %.3f | %5.1f |\n",
                    r.name.c_str(), r.clips, pct(r.ex_strict, r.tempo_clips), pct(r.ex_oct, r.tempo_clips),
                    pct(r.tr_strict, r.tempo_clips), pct(r.tr_oct, r.tempo_clips), pct(r.af_strict, r.tempo_clips),
                    pct(r.af_oct, r.tempo_clips), pct(r.ex_band, r.band_clips), pct(r.af_band, r.band_clips),
                    r.on_before / n, r.on_after / n, r.beat_before / nt, r.beat_after / nt, pct(r.reliable, r.clips));
    } else {
        std::printf("| %-22s | %3d |       -       |       -       |       -       |       -        | %.3f -> %.3f |       -        | %5.1f |\n",
                    r.name.c_str(), r.clips, r.on_before / n, r.on_after / n, pct(r.reliable, r.clips));
    }
}

std::vector<double> Tempi() {
    std::vector<double> t;
    for (double b = 50; b <= 180; b += 10) t.push_back(b);
    return t;
}

void Families(Row& total) {
    std::printf("\n## Onsets, tempo, beats (30 s clips, 11025 Hz)\n\n");
    std::printf("Tempo: %% of clips within 4 %% (strict / octave-corrected x1/2 x1 x2). Onset F at 50 ms, beat F at 70 ms, before -> after. Reliable: %% of clips AssessTempo trusts.\n\n");
    std::printf("| family | clips | existing (loudness peaks) | trivial ACF | after (EstimateTempo) | strict, truth <= 130 BPM: existing -> after | onset F | beat F | reliable %% |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|---:|\n");
    struct Fam {
        const char* name;
        void (*apply)(synth::Spec&);
        bool tempo;
    };
    static const Fam fams[] = {
        {"clean", [](synth::Spec&) {}, true},
        {"jitter 20 ms", [](synth::Spec& s) { s.jitter_ms = 20; }, true},
        {"noise 0.15", [](synth::Spec& s) { s.noise = 0.15; }, true},
        {"sparse (no pad)", [](synth::Spec& s) { s.pad = 0; }, true},
        {"off-beat hats", [](synth::Spec& s) { s.offbeat_hats = true; }, true},
        {"10 s silence inside", [](synth::Spec& s) { s.silence_from = 10; s.silence_to = 20; }, true},
        {"jitter+noise+hats", [](synth::Spec& s) { s.jitter_ms = 15; s.noise = 0.08; s.offbeat_hats = true; }, true},
    };
    std::uint64_t seed = 11;
    for (const auto& f : fams) {
        Row r;
        r.name = f.name;
        for (double bpm : Tempi()) {
            synth::Spec s;
            s.bpm = bpm;
            s.seed = seed++;
            f.apply(s);
            Score(r, synth::Make(s), f.tempo);
        }
        PrintRow(r);
        total.clips += r.clips;
        total.tempo_clips += r.tempo_clips;
        total.ex_strict += r.ex_strict;
        total.ex_oct += r.ex_oct;
        total.tr_strict += r.tr_strict;
        total.tr_oct += r.tr_oct;
        total.af_strict += r.af_strict;
        total.af_oct += r.af_oct;
        total.band_clips += r.band_clips;
        total.ex_band += r.ex_band;
        total.af_band += r.af_band;
        total.on_before += r.on_before;
        total.on_after += r.on_after;
        total.beat_before += r.beat_before;
        total.beat_after += r.beat_after;
        total.reliable += r.reliable;
    }
    total.name = "all tempo families";
    PrintRow(total);
    // No tempo: what matters is that the gate says so.
    Row speech, noise, silence, pad;
    speech.name = "speech-like (6 seeds)";
    for (std::uint64_t k = 1; k <= 6; ++k) {
        synth::Spec s;
        s.hits = false;
        s.pad = 0;
        s.speech = true;
        s.seed = 100 + k;
        Score(speech, synth::Make(s), false);
    }
    noise.name = "steady noise";
    silence.name = "silence";
    pad.name = "chord pad only";
    for (std::uint64_t k = 1; k <= 3; ++k) {
        synth::Spec s;
        s.hits = false;
        s.pad = 0;
        s.seed = 200 + k;
        s.noise = 0.05 * double(k);
        Score(noise, synth::Make(s), false);
        s.noise = 0;
        Score(silence, synth::Make(s), false);
        s.pad = 0.05 * double(k);
        Score(pad, synth::Make(s), false);
    }
    PrintRow(speech);
    PrintRow(noise);
    PrintRow(silence);
    PrintRow(pad);
}

// Tempo changes: 10 s windows (hop 5 s) that lie inside one tempo.
void TempoChanges() {
    std::printf("\n## Tempo changes (60 s clips, x1.25 at 30 s; 10 s windows inside one tempo)\n\n");
    int windows = 0, ex = 0, ex_o = 0, af = 0, af_o = 0, whole_af = 0;
    for (double bpm : {56.0, 64.0, 72.0, 80.0, 96.0, 112.0}) {
        synth::Spec s;
        s.seconds = 60;
        s.bpm = bpm;
        s.bpm2 = bpm * 1.25;
        s.jitter_ms = 10;
        s.seed = std::uint64_t(bpm);
        const auto c = synth::Make(s);
        const After a = RunAfter(c.samples, s.sample_rate);
        whole_af += synth::TempoOctaveWithin(a.tempo.bpm, bpm) || synth::TempoOctaveWithin(a.tempo.bpm, bpm * 1.25);
        for (double w0 = 0; w0 + 10 <= 60; w0 += 5) {
            if (w0 < 30 && w0 + 10 > 30) continue;  // straddles the change
            const double truth = w0 >= 30 ? bpm * 1.25 : bpm;
            const auto i0 = std::size_t(w0 * a.fps), n = std::size_t(10 * a.fps);
            const auto t = samn::EstimateTempo(a.curve.data() + i0, std::min(n, a.curve.size() - i0), a.fps);
            const std::vector<float> part(c.samples.begin() + std::ptrdiff_t(w0 * s.sample_rate),
                                          c.samples.begin() + std::ptrdiff_t((w0 + 10) * s.sample_rate));
            const Before b = RunBefore(part, s.sample_rate, 10);
            ++windows;
            ex += synth::TempoWithin(b.bpm_existing, truth);
            ex_o += synth::TempoOctaveWithin(b.bpm_existing, truth);
            af += synth::TempoWithin(t.bpm, truth);
            af_o += synth::TempoOctaveWithin(t.bpm, truth);
        }
    }
    std::printf("| windows | existing strict / octave | after strict / octave | after, whole clip matches either tempo (octave) |\n|---:|---:|---:|---:|\n");
    std::printf("| %d | %.1f / %.1f | %.1f / %.1f | %d of 6 |\n", windows, 100.0 * ex / windows, 100.0 * ex_o / windows,
                100.0 * af / windows, 100.0 * af_o / windows, whole_af);
}

void Speed() {
    std::printf("\n## Cost (10 min clip, 11025 Hz, 96 BPM with hats, noise and jitter)\n\n");
    synth::Spec s;
    s.seconds = 600;
    s.bpm = 96;
    s.jitter_ms = 10;
    s.offbeat_hats = true;
    s.noise = 0.05;
    const auto c = synth::Make(s);
    const double secs = s.seconds;
    auto t0 = Clock::now();
    samn::WindowRms w(std::size_t(s.sample_rate * samn::kAudioLevelStepMs / 1000));
    w.Push(c.samples.data(), c.samples.size());
    const auto loud = samn::NormalizeLoudness(w.Finish());
    volatile double sink = samn::EstimateBpmFromLoudness(loud);
    auto t1 = Clock::now();
    const long long base = ResetPeak();
    samn::OnsetCurve oc(s.sample_rate);
    oc.Push(c.samples.data(), c.samples.size());
    oc.Finish();
    auto t2 = Clock::now();
    const auto tempo = samn::EstimateTempo(oc.Values(), oc.FramesPerSecond());
    auto t3 = Clock::now();
    const auto beats = samn::TrackBeats(oc.Values(), oc.FramesPerSecond(), tempo.bpm);
    auto t4 = Clock::now();
    const long long heap = g_peak.load() - base;
    sink = sink + tempo.bpm + double(beats.beats_ms.size());
    std::printf("| step | ms per second of sound |\n|---|---:|\n");
    std::printf("| before: 50 ms RMS + loudness + EstimateBpmFromLoudness | %.4f |\n", Ms(t0, t1) / secs);
    std::printf("| OnsetCurve (FFT, band flux, gate) | %.4f |\n", Ms(t1, t2) / secs);
    std::printf("| EstimateTempo | %.4f |\n", Ms(t2, t3) / secs);
    std::printf("| TrackBeats | %.4f |\n", Ms(t3, t4) / secs);
    std::printf("\nPeak heap of the after analysis on 10 min, outputs kept: %.1f KiB (onset curve %zu values, %zu beats).\n",
                double(heap) / 1024.0, oc.Values().size(), beats.beats_ms.size());
}

bool Memory10h(bool print) {
    constexpr int kSeconds = 10 * 3600;
    std::vector<float> second(samn::kOnsetSampleRate, 0.0f), noise(samn::kOnsetSampleRate);
    synth::Rng rng(5);
    for (float& v : noise) v = float(0.01 * rng.Gauss());  // one second of quiet noise, reused
    const long long base = ResetPeak();
    auto t0 = Clock::now();
    std::uint64_t frames = 0;
    double bpm = 0;
    {
        samn::OnsetCurve oc;
        samn::TempoAccumulator acc(oc.FramesPerSecond());
        for (int s = 0; s < kSeconds; ++s) {
            // A drum-like click on every beat of 75 BPM plus quiet noise.
            second = noise;
            const double first = std::fmod(0.8 - std::fmod(double(s), 0.8), 0.8);
            for (double t = first; t < 1.0; t += 0.8) {
                const auto at = std::size_t(t * samn::kOnsetSampleRate);
                for (std::size_t i = 0; i < 400 && at + i < second.size(); ++i)
                    second[at + i] += float(0.6 * std::sin(0.08 * double(i)) * std::exp(-double(i) / 120.0));
            }
            oc.Push(second.data(), second.size());
            std::vector<float> v = oc.TakeValues();
            acc.Push(v.data(), v.size());
            oc.TakeBlocks();
        }
        oc.Finish();
        const auto tail = oc.TakeValues();
        acc.Push(tail.data(), tail.size());
        frames = oc.Frames();
        bpm = acc.Estimate().bpm;
    }
    const double ms = Ms(t0, Clock::now());
    const long long peak = g_peak.load() - base;
    const bool ok = peak <= kMemoryBound;
    if (print) {
        std::printf("\n## 10 hours streamed (11025 Hz, 1 s chunks, outputs drained)\n\n");
        std::printf("| frames | peak heap of the analysis | bound | tempo | wall time | ms per second of sound |\n|---:|---:|---:|---:|---:|---:|\n");
        std::printf("| %llu | %.1f KiB | %.0f KiB | %.1f BPM | %.1f s | %.4f |\n", (unsigned long long)frames,
                    double(peak) / 1024.0, double(kMemoryBound) / 1024.0, bpm, ms / 1000.0, ms / kSeconds);
    } else {
        std::printf("10 h: %llu frames, peak heap %lld bytes (bound %lld), tempo %.1f BPM: %s\n",
                    (unsigned long long)frames, peak, kMemoryBound, bpm, ok ? "ok" : "OVER THE BOUND");
    }
    return ok && synth::TempoOctaveWithin(bpm, 75);
}

// ---- Gap fill: linear vs heart rhythm --------------------------------------

struct Gap {
    int64_t from, to;
};

double Interp(const std::vector<samn::Action>& a, double t) {
    if (a.empty()) return 50;
    if (t <= double(a.front().at)) return a.front().pos;
    if (t >= double(a.back().at)) return a.back().pos;
    const auto it = std::lower_bound(a.begin(), a.end(), t, [](const samn::Action& x, double v) { return double(x.at) < v; });
    const auto& n = *it;
    const auto& p = *(it - 1);
    if (n.at == p.at) return n.pos;
    return p.pos + (t - double(p.at)) / double(n.at - p.at) * (n.pos - p.pos);
}

std::vector<samn::Action> Slice(const std::vector<samn::Action>& a, int64_t from, int64_t to) {
    std::vector<samn::Action> out{{from, int(std::lround(Interp(a, double(from))))}};
    for (const auto& x : a)
        if (x.at > from && x.at < to) out.push_back(x);
    out.push_back({to, int(std::lround(Interp(a, double(to))))});
    return out;
}

// A stroke script at spm strokes per minute with slow drift, plus gaps.
std::vector<samn::Action> StrokeScript(double spm, double seconds, bool hold_in_gaps, const std::vector<Gap>& gaps,
                                       std::uint64_t seed) {
    synth::Rng rng(seed);
    std::vector<samn::Action> a;
    int k = 0;
    double t = 0;
    while (t < seconds * 1000) {
        const double drift = 1 + 0.05 * std::sin(t / 17000.0 + double(seed));
        const double amp = 40 + 20 * std::sin(t / 9000.0 + double(seed) * 0.7);
        const double centre = 50 + 8 * std::sin(t / 23000.0);
        const auto at = int64_t(std::llround(t));
        bool inside = false;
        for (const auto& g : gaps)
            if (at > g.from && at < g.to) inside = true;
        int pos = int(std::lround(centre + (k % 2 ? amp / 2 : -amp / 2)));
        if (hold_in_gaps && inside && !a.empty()) pos = a.back().pos;
        a.push_back({at, std::clamp(pos, 0, 100)});
        t += 30000.0 / (spm * drift) * (1 + 0.03 * (rng.Uniform() - 0.5));
        ++k;
    }
    return a;
}

std::vector<samn::Action> Without(const std::vector<samn::Action>& a, const std::vector<Gap>& gaps) {
    std::vector<samn::Action> out;
    for (const auto& x : a) {
        bool inside = false;
        for (const auto& g : gaps)
            if (x.at > g.from && x.at < g.to) inside = true;
        if (!inside) out.push_back(x);
    }
    return out;
}

struct FillScore {
    double doctor = 0, gap_r = 0, gap_r0 = 0, gap_mae = 0;
    int gaps = 0;
};

void ScoreFill(const std::vector<samn::Action>& ref, const std::vector<samn::Action>& filled, const std::vector<Gap>& gaps,
               FillScore& s) {
    s.doctor += samn::EvaluateScriptQuality(filled).score;
    for (const auto& g : gaps) {
        // Between the anchors either side of the hole (the fill's real span).
        const auto rs = Slice(ref, g.from, g.to), fs = Slice(filled, g.from, g.to);
        const auto c = samn::BestLagCorrelation(rs, fs);
        s.gap_r += c ? c->r : 0.0;
        double sxy = 0, sxx = 0, syy = 0, mx = 0, my = 0, mae = 0;
        int n = 0;
        for (int64_t t = g.from; t <= g.to; t += 50) {
            mx += Interp(ref, double(t));
            my += Interp(filled, double(t));
            ++n;
        }
        mx /= n;
        my /= n;
        for (int64_t t = g.from; t <= g.to; t += 50) {
            const double x = Interp(ref, double(t)) - mx, y = Interp(filled, double(t)) - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
            mae += std::fabs(Interp(ref, double(t)) - Interp(filled, double(t)));
        }
        s.gap_r0 += sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
        s.gap_mae += mae / n;
        ++s.gaps;
    }
}

bool FillBench() {
    std::printf("\n## Gap fill: linear (default) vs heart rhythm (generator.rhythmic_fill)\n\n");
    std::printf("120 s stroke scripts, 4 holes of 1.5-5 s per script, ImproveScript fill_gaps with the auto threshold, no audio tempo.\n");
    std::printf("Doctor = EvaluateScriptQuality score (existing); gap r = BestLagCorrelation inside the holes (existing, +-1 s lag, best orientation; undefined = 0);\n");
    std::printf("gap r0 = Pearson at lag 0 and gap MAE = mean |position error| in the holes on a 50 ms grid (new, this bench).\n\n");
    std::printf("| clips | n | Doctor linear -> rhythmic | gap r linear -> rhythmic | gap r0 linear -> rhythmic | gap MAE linear -> rhythmic |\n|---|---:|---:|---:|---:|---:|\n");
    bool ok = true;
    for (int kind = 0; kind < 3; ++kind) {
        FillScore lin, rhy;
        int n = 0;
        for (double spm : {45.0, 60.0, 72.0, 85.0, 100.0, 120.0}) {
            for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                const std::vector<Gap> gaps{{int64_t(15000 + 1000 * seed), int64_t(16500 + 1000 * seed)},
                                            {40000, 43000},
                                            {int64_t(70000 + 700 * seed), int64_t(74000 + 700 * seed)},
                                            {95000, 100000}};
                const auto ref = StrokeScript(spm, 120, kind == 1, gaps, seed);
                auto input = Without(ref, gaps);
                if (kind == 2) {
                    // No strokes near the holes: only every 8th point stays.
                    std::vector<samn::Action> thin;
                    for (std::size_t i = 0; i < input.size(); i += 8) thin.push_back(input[i]);
                    input = thin;
                }
                samn::ImproveOptions o;
                o.fill_gaps = true;
                const auto linear = samn::ImproveScript(input, o).actions;
                o.rhythmic_fill = true;
                const auto rhythmic = samn::ImproveScript(input, o).actions;
                ScoreFill(ref, linear, gaps, lin);
                ScoreFill(ref, rhythmic, gaps, rhy);
                ++n;
            }
        }
        const char* names[] = {"strokes through the holes", "script holds in the holes", "sparse script (heart rhythm)"};
        const double g = std::max(1, lin.gaps);
        std::printf("| %s | %d | %.3f -> %.3f | %.3f -> %.3f | %.3f -> %.3f | %.1f -> %.1f |\n", names[kind], n,
                    lin.doctor / n, rhy.doctor / n, lin.gap_r / g, rhy.gap_r / g, lin.gap_r0 / g, rhy.gap_r0 / g,
                    lin.gap_mae / g, rhy.gap_mae / g);
        if (kind < 2 && (rhy.doctor < lin.doctor - 1e-9 || rhy.gap_r < lin.gap_r - 1e-9)) ok = false;
    }
    std::printf("\nRhythmic at least as good as linear on the existing metrics (Doctor, gap r) where the script strokes or holds: %s\n",
                ok ? "yes" : "NO");
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--memory-10h") == 0) return Memory10h(false) ? 0 : 1;
    std::printf("# audio_onset_bench\n");
    Row total;
    Families(total);
    TempoChanges();
    Speed();
    const bool mem = Memory10h(true);
    const bool fill = FillBench();
    std::printf("\nPeak RSS of the whole bench process: %ld KiB\n", PeakRssKb());
    // The fill comparison informs the default (D-168); only the memory bound fails the run.
    (void)fill;
    return mem ? 0 : 1;
}
