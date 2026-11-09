#pragma once
// Synthetic sound with a known truth for the onset, tempo and beat tests and
// for tests/bench/audio_onset_bench.cpp: drum hits on a beat grid (with
// jitter, tempo changes, silence, off-beat hats, a chord pad, noise) and
// speech-like syllables. Generated in code, deterministic (seeded); no
// recorded media.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace synth {

constexpr double kPi = 3.14159265358979323846;

class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed * 2862933555777941757ULL + 3037000493ULL) {}
    std::uint64_t Next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 7;
        s_ ^= s_ << 17;
        return s_;
    }
    double Uniform() { return double(Next() >> 11) / double(1ULL << 53); }  // [0, 1)
    double Gauss() {
        const double u = std::max(1e-12, Uniform()), v = Uniform();
        return std::sqrt(-2 * std::log(u)) * std::cos(2 * kPi * v);
    }

private:
    std::uint64_t s_;
};

struct Spec {
    std::string name;
    double seconds = 30;
    int sample_rate = 11025;
    double bpm = 90;
    double bpm2 = 0;            // > 0: the tempo after change_at
    double change_at = 0.5;     // share of the clip
    double jitter_ms = 0;       // standard deviation per hit
    double noise = 0;           // white noise amplitude
    double pad = 0.08;          // chord pad amplitude (0 = sparse hits only)
    double hit = 0.6;           // drum hit amplitude
    bool offbeat_hats = false;  // a quieter hat between the beats
    double silence_from = -1;   // seconds; hits and pad muted in [from, to)
    double silence_to = -1;
    bool hits = true;
    bool speech = false;        // speech-like syllables instead of / with hits
    std::uint64_t seed = 1;
};

struct Clip {
    Spec spec;
    std::vector<float> samples;
    std::vector<double> onsets_ms;  // every hit and syllable start
    std::vector<double> beats_ms;   // the drum beats only
};

inline bool Muted(const Spec& s, double t) { return t >= s.silence_from && t < s.silence_to; }

// Kick-like hit: a falling sine plus a short noise click.
inline void AddHit(std::vector<float>& out, int sr, double t0, double amp, Rng& rng, bool hat) {
    const auto start = std::int64_t(std::llround(t0 * sr));
    const int len = int(0.25 * sr);
    double phase = 0;
    for (int i = 0; i < len; ++i) {
        const std::int64_t k = start + i;
        if (k < 0 || k >= std::int64_t(out.size())) continue;
        const double t = double(i) / sr;
        double v;
        if (hat) {
            v = 0.5 * rng.Gauss() * std::exp(-t / 0.012);
        } else {
            const double f = 50 + 70 * std::exp(-t / 0.03);
            phase += 2 * kPi * f / sr;
            v = std::sin(phase) * std::exp(-t / 0.08) + 0.4 * rng.Gauss() * std::exp(-t / 0.004);
        }
        out[std::size_t(k)] += float(amp * v);
    }
}

// One voiced syllable: harmonics of a gliding F0 shaped by two formants.
inline void AddSyllable(std::vector<float>& out, int sr, double t0, double dur, double amp, Rng& rng) {
    const double f0 = 110 + 110 * rng.Uniform(), glide = 0.8 + 0.4 * rng.Uniform();
    const double f1 = 400 + 500 * rng.Uniform(), f2 = 1100 + 1200 * rng.Uniform();
    const auto start = std::int64_t(std::llround(t0 * sr));
    const int len = int(dur * sr);
    double phase = 0;
    for (int i = 0; i < len; ++i) {
        const std::int64_t k = start + i;
        if (k < 0 || k >= std::int64_t(out.size())) continue;
        const double x = double(i) / len;
        const double env = std::sin(kPi * x) * std::sin(kPi * x);
        const double f = f0 * (1 + (glide - 1) * x);
        phase += 2 * kPi * f / sr;
        double v = 0;
        for (int h = 1; h <= 12; ++h) {
            const double fh = f * h;
            if (fh > sr / 2.2) break;
            const double w = std::exp(-std::pow((fh - f1) / 250, 2)) + 0.6 * std::exp(-std::pow((fh - f2) / 350, 2)) + 0.05;
            v += w * std::sin(h * phase);
        }
        out[std::size_t(k)] += float(amp * env * v * 0.4);
    }
}

inline Clip Make(const Spec& s) {
    Clip c;
    c.spec = s;
    const int sr = s.sample_rate;
    c.samples.assign(std::size_t(s.seconds * sr), 0.0f);
    Rng rng(s.seed);
    if (s.hits) {
        double t = 0.5;
        while (t < s.seconds - 0.3) {
            const double bpm = (s.bpm2 > 0 && t >= s.change_at * s.seconds) ? s.bpm2 : s.bpm;
            const double period = 60.0 / bpm;
            const double at = t + s.jitter_ms / 1000.0 * rng.Gauss();
            if (!Muted(s, at)) {
                AddHit(c.samples, sr, at, s.hit, rng, false);
                c.onsets_ms.push_back(at * 1000);
                c.beats_ms.push_back(at * 1000);
            }
            const double off = at + period / 2;
            if (s.offbeat_hats && off < s.seconds - 0.3 && !Muted(s, off)) {
                AddHit(c.samples, sr, off, s.hit * 0.5, rng, true);
                c.onsets_ms.push_back(off * 1000);
            }
            t += period;
        }
    }
    if (s.speech) {
        double t = 0.3;
        while (t < s.seconds - 0.4) {
            const double dur = 0.08 + 0.22 * rng.Uniform();
            if (!Muted(s, t)) {
                AddSyllable(c.samples, sr, t, dur, 0.5, rng);
                c.onsets_ms.push_back(t * 1000);
            }
            t += dur + 0.04 + 0.26 * rng.Uniform();
            if (rng.Uniform() < 0.12) t += 0.4 + 0.6 * rng.Uniform();  // phrase pause
        }
    }
    const double chord[] = {220.0, 277.18, 329.63};
    for (std::size_t i = 0; i < c.samples.size(); ++i) {
        const double t = double(i) / sr;
        double v = 0;
        if (s.pad > 0 && !Muted(s, t))
            for (double f : chord) v += s.pad / 3 * std::sin(2 * kPi * f * t);
        if (s.noise > 0) v += s.noise * rng.Gauss();
        c.samples[i] = float(std::clamp(double(c.samples[i]) + v, -1.0, 1.0));
    }
    std::sort(c.onsets_ms.begin(), c.onsets_ms.end());
    return c;
}

// Greedy one-to-one matching within tol_ms: F-measure (1 when both empty).
inline double FMeasure(const std::vector<double>& truth, const std::vector<double>& found, double tol_ms) {
    if (truth.empty() && found.empty()) return 1;
    if (truth.empty() || found.empty()) return 0;
    std::size_t i = 0, j = 0, hits = 0;
    while (i < truth.size() && j < found.size()) {
        const double d = found[j] - truth[i];
        if (std::fabs(d) <= tol_ms) {
            ++hits;
            ++i;
            ++j;
        } else if (d < 0) {
            ++j;
        } else {
            ++i;
        }
    }
    const double p = double(hits) / double(found.size()), r = double(hits) / double(truth.size());
    return p + r > 0 ? 2 * p * r / (p + r) : 0;
}

inline bool TempoWithin(double est, double truth, double tol = 0.04) {
    return est > 0 && truth > 0 && std::fabs(est - truth) <= tol * truth;
}
inline bool TempoOctaveWithin(double est, double truth, double tol = 0.04) {
    return TempoWithin(est, truth, tol) || TempoWithin(est, truth * 2, tol) || TempoWithin(est, truth / 2, tol);
}

}  // namespace synth
