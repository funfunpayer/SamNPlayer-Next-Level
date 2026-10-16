#include "samn/heatmap_image.h"

#include <cmath>
#include <stdexcept>

namespace samn {

Rgba HeatmapColor(double n) {
    if (n < 0) n = 0;
    if (n > 1) n = 1;
    auto u8 = [](double v) { return static_cast<uint8_t>(v); };  // truncates, as Go
    if (n < 0.35) {
        const double t = n / 0.35;
        return {u8(20 + 27 * t), u8(40 + 172 * t), u8(60 + 136 * t), 255};
    }
    if (n < 0.7) {
        const double t = (n - 0.35) / 0.35;
        return {u8(47 + 196 * t), u8(212 - 34 * t), u8(196 - 136 * t), 255};
    }
    const double t = (n - 0.7) / 0.3;
    return {243, u8(178 - 80 * t), u8(60 - 20 * t), 255};
}

HeatmapImage RenderHeatmapImage(const std::vector<Action>& actions, const HeatmapImageOptions& opts) {
    if (actions.size() < 2) throw std::invalid_argument("funscript: heatmap needs at least 2 actions");
    const int w = opts.width > 0 ? opts.width : 800;
    const int h = opts.height > 0 ? opts.height : 48;
    int64_t dur = actions.back().at - actions.front().at;
    if (dur <= 0) dur = 1;

    const int64_t buckets = w;
    std::vector<double> intens(static_cast<size_t>(buckets), 0.0);
    std::vector<double> counts(static_cast<size_t>(buckets), 0.0);
    for (size_t i = 0; i + 1 < actions.size(); ++i) {
        const double dt = static_cast<double>(actions[i + 1].at - actions[i].at);
        if (dt <= 0) continue;
        const double v = 500.0 * std::fabs(static_cast<double>(actions[i + 1].pos - actions[i].pos)) / dt;
        const int64_t mid = actions[i].at + (actions[i + 1].at - actions[i].at) / 2;
        int64_t idx = (mid - actions.front().at) * buckets / dur;
        if (idx < 0) idx = 0;
        if (idx >= buckets) idx = buckets - 1;
        intens[static_cast<size_t>(idx)] += v;
        counts[static_cast<size_t>(idx)]++;
    }
    double peak = 0;
    for (size_t i = 0; i < intens.size(); ++i) {
        if (counts[i] > 0) intens[i] /= counts[i];
        if (intens[i] > peak) peak = intens[i];
    }
    if (peak <= 0) peak = 1;

    HeatmapImage img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    auto set = [&](int x, int y, Rgba c) {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
        img.rgba[i] = c.r;
        img.rgba[i + 1] = c.g;
        img.rgba[i + 2] = c.b;
        img.rgba[i + 3] = c.a;
    };
    const Rgba bg{12, 16, 24, 255};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) set(x, y, bg);
    for (int x = 0; x < w; ++x) {
        const double n = intens[static_cast<size_t>(x)] / peak;
        const Rgba col = HeatmapColor(n);
        int bar_h = static_cast<int>(std::round(n * static_cast<double>(h - 4)));
        if (bar_h < 1 && intens[static_cast<size_t>(x)] > 0) bar_h = 1;
        for (int y = h - 2; y > h - 2 - bar_h && y >= 0; --y) set(x, y, col);
    }
    const Rgba tick{47, 212, 196, 255};
    for (const Chapter& ch : opts.chapters) {
        const int64_t x = (ch.start_ms - actions.front().at) * static_cast<int64_t>(w) / dur;
        if (x < 0 || x >= w) continue;
        for (int y = 0; y < h; ++y) set(static_cast<int>(x), y, tick);
    }
    return img;
}

}  // namespace samn
