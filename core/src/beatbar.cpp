#include "samn/beatbar.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

namespace samn {

constexpr int kBands = 24;
constexpr int kMaxShift = 40;
constexpr double kMinCorr = 0.6;
constexpr double kMinScore = 0.35;
constexpr int kBgFrames = 15;

std::vector<double> bandProfile(const GrayFrame& f, int y0, int y1) {
    std::vector<double> p(f.w, 0.0);
    if (y1 <= y0) return p;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = f.pix.data() + y * f.w;
        for (int x = 0; x < f.w; ++x) {
            p[x] += row[x];
        }
    }
    double n = y1 - y0;
    for (auto& v : p) v /= n;
    return p;
}

std::pair<int, double> bestShift(const std::vector<double>& a, const std::vector<double>& b, int maxShift) {
    double bestCorr = -2.0;
    int bestS = 0;
    for (int s = -maxShift; s <= maxShift; ++s) {
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
        int n = 0;
        for (size_t x = 0; x < b.size(); ++x) {
            int xa = static_cast<int>(x) - s;
            if (xa < 0 || xa >= static_cast<int>(a.size())) continue;
            double va = a[xa], vb = b[x];
            sa += va; sb += vb;
            saa += va * va; sbb += vb * vb; sab += va * vb;
            ++n;
        }
        if (n < static_cast<int>(b.size()) / 2) continue;
        double fn = n;
        double cov = sab - sa * sb / fn;
        double den = std::sqrt((saa - sa * sa / fn) * (sbb - sb * sb / fn));
        if (den <= 1e-9) continue;
        double c = cov / den;
        if (c > bestCorr) {
            bestCorr = c;
            bestS = s;
        }
    }
    return {bestS, bestCorr};
}

class BgRemover {
public:
    std::vector<double> clean(const std::vector<double>& raw) {
        window_.push_back(raw);
        if (window_.size() > static_cast<size_t>(kBgFrames)) {
            window_.erase(window_.begin());
        }
        std::vector<double> out(raw.size(), 0.0);
        std::vector<double> col(window_.size());
        for (size_t x = 0; x < raw.size(); ++x) {
            for (size_t k = 0; k < window_.size(); ++k) {
                col[k] = window_[k][x];
            }
            std::sort(col.begin(), col.end());
            double d = raw[x] - col[col.size() / 2];
            if (d > 0) out[x] = d;
        }
        return out;
    }
private:
    std::vector<std::vector<double>> window_;
};

bool flatProfile(const std::vector<double>& p) {
    for (double v : p) if (v > 8) return false;
    return true;
}

double weightedShift(const std::map<int, int>& shifts, int mode) {
    double sum = 0, n = 0;
    for (int s : {mode - 1, mode, mode + 1}) {
        auto it = shifts.find(s);
        if (it != shifts.end()) {
            sum += s * it->second;
            n += it->second;
        }
    }
    return n == 0 ? mode : sum / n;
}

std::vector<double> peaksOf(const std::vector<double>& p) {
    std::vector<double> out;
    for (size_t x = 1; x + 1 < p.size(); ++x) {
        double v = p[x];
        if (v < 20 || v < p[x - 1] || v < p[x + 1]) continue;
        // Plateau: report its centre once.
        size_t end = x;
        while (end + 1 < p.size() && p[end + 1] == v) ++end;
        out.push_back(static_cast<double>(x + end) / 2.0);
        x = end;
    }
    return out;
}

double findTarget(const std::vector<GrayFrame>& frames, const BeatBar& bar) {
    BgRemover bg;
    std::vector<double> xs;
    for (const auto& f : frames) {
        auto p = bg.clean(bandProfile(f, bar.y0, bar.y1));
        auto peaks = peaksOf(p);
        xs.insert(xs.end(), peaks.begin(), peaks.end());
    }
    if (xs.empty()) return 0;
    std::sort(xs.begin(), xs.end());
    double q = 0.02;
    if (bar.velocity > 0) q = 0.98;
    size_t idx = static_cast<size_t>(q * (xs.size() - 1));
    return xs[idx];
}

std::optional<BeatBar> FindBeatBar(const std::vector<GrayFrame>& frames) {
    if (frames.size() < 10) return std::nullopt;
    int w = frames[0].w, h = frames[0].h;
    int bandH = h / kBands;
    if (bandH < 2 || w < 32) return std::nullopt;
    int maxShift = std::min(kMaxShift, w / 4);
    BeatBar best{0, 0, 0, 0, -1};
    for (int b = 0; b < kBands; ++b) {
        int y0 = b * bandH, y1 = (b + 1) * bandH;
        BgRemover bg;
        std::vector<std::vector<double>> profiles;
        profiles.reserve(frames.size());
        for (const auto& f : frames) {
            profiles.push_back(bg.clean(bandProfile(f, y0, y1)));
        }
        std::map<int, int> shifts;
        int steady = 0;
        for (size_t i = 1; i < profiles.size(); ++i) {
            if (flatProfile(profiles[i]) || flatProfile(profiles[i - 1])) continue;
            auto [s, c] = bestShift(profiles[i - 1], profiles[i], maxShift);
            if (c >= kMinCorr && s != 0) {
                shifts[s]++;
                ++steady;
            }
        }
        if (steady == 0) continue;
        int mode = 0, modeN = 0;
        for (const auto& [s, n] : shifts) {
            if (n > modeN || (n == modeN && std::abs(s) < std::abs(mode))) {
                mode = s;
                modeN = n;
            }
        }
        int near = modeN + shifts[mode - 1] + shifts[mode + 1];
        double score = static_cast<double>(near) / (frames.size() - 1);
        if (score > best.score) {
            double v = weightedShift(shifts, mode);
            best = {y0, y1, v, 0, score};
        }
    }
    if (best.score < kMinScore) return std::nullopt;
    best.target_x = findTarget(frames, best);
    return best;
}

class BeatTracker {
public:
    BeatTracker(const BeatBar& bar, double fps)
        : bar_(bar), frameMs_(1000.0 / fps), minGapMs_(std::max(2 * frameMs_, 80.0)) {}

    void add(const std::vector<double>& raw) {
        auto p = bg_.clean(raw);
        double v = bar_.velocity;
        double speed = std::abs(v);
        for (double x : peaksOf(p)) {
            // Distance still to go toward the target along the motion.
            double d = x - bar_.target_x;
            if (v > 0) d = bar_.target_x - x;
            if (d < 0 || d >= speed) continue;
            double at = (static_cast<double>(frame_) + d / speed) * frameMs_;
            if (!beats_.empty() && at - beats_.back() < minGapMs_) continue;
            beats_.push_back(at);
        }
        ++frame_;
    }

    const std::vector<double>& beats() const { return beats_; }

private:
    BeatBar bar_;
    double frameMs_;
    double minGapMs_;
    BgRemover bg_;
    int frame_ = 0;
    std::vector<double> beats_;
};

std::vector<double> BeatTimes(const std::vector<GrayFrame>& frames, const BeatBar& bar, double fps) {
    if (fps <= 0 || bar.velocity == 0) return {};
    BeatTracker tr(bar, fps);
    for (const auto& f : frames) {
        tr.add(bandProfile(f, bar.y0, bar.y1));
    }
    return tr.beats();
}

}  // namespace samn
