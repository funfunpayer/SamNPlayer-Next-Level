#include "beat_strokes.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace samn {

namespace {

int clampPos(int p) {
    return std::max(0, std::min(100, p));
}

const double beatLeadMs = 400.0;

}  // namespace

std::vector<Action> BeatStrokes(const std::vector<double>& beatsMs, const BeatStrokeOptions& opts) {
    BeatStrokeOptions o = opts;
    if (o.top == 0 && o.bottom == 0) {
        o.top = 90;
        o.bottom = 10;
    }
    o.top = clampPos(o.top);
    o.bottom = clampPos(o.bottom);
    if (o.top < o.bottom) {
        std::swap(o.top, o.bottom);
    }
    if (o.maxSpeed <= 0) {
        o.maxSpeed = 500;
    }
    if (o.holdAfterMs <= 0) {
        o.holdAfterMs = 2000;
    }

    std::vector<double> beats = beatsMs;
    std::sort(beats.begin(), beats.end());

    std::vector<Action> out;
    auto add = [&](double at, int pos) {
        int64_t t = static_cast<int64_t>(std::round(at));
        if (t < 0) t = 0;
        if (!out.empty() && t <= out.back().at) return;
        out.push_back({t, pos});
    };

    auto up = [&](double ms) -> int {
        double h = static_cast<double>(o.bottom) + o.maxSpeed * ms / 1000.0;
        if (h > static_cast<double>(o.top)) h = static_cast<double>(o.top);
        return static_cast<int>(std::floor(h));
    };

    for (size_t i = 0; i < beats.size(); ++i) {
        double b = beats[i];
        if (i == 0) {
            double lead = std::min(beatLeadMs, b);
            if (lead > 0) {
                add(b - lead, up(lead));
            }
        }
        add(b, o.bottom);
        if (i + 1 >= beats.size()) break;
        double gap = beats[i + 1] - b;
        if (gap > o.holdAfterMs) {
            int h = up(beatLeadMs);
            add(b + beatLeadMs, h);
            add(beats[i + 1] - beatLeadMs, h);
            continue;
        }
        add(b + gap / 2.0, up(gap / 2.0));
    }
    return out;
}

}  // namespace samn
