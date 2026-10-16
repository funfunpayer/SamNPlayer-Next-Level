#pragma once
// Intensity heatmap as an RGBA pixel buffer (0.7 ExportHeatmapPNG). Same
// buckets, colours and chapter ticks as Go, pixel for pixel. The core has no
// PNG encoder: the app turns the buffer into a PNG (e.g. QImage).
//
// Intensity is the community formula 500 x |dpos| / dt; chapter starts are
// thin teal ticks. Errors: std::invalid_argument with the Go message.
#include <cstdint>
#include <vector>

#include "samn/funscript.h"
#include "samn/marks.h"

namespace samn {

struct HeatmapImageOptions {
    int width = 0;   // <= 0 means 800
    int height = 0;  // <= 0 means 48
    std::vector<Chapter> chapters;
};

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// Row-major, top row first, 4 bytes (R, G, B, A) per pixel, straight alpha.
struct HeatmapImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4 bytes
    Rgba At(int x, int y) const {
        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
    }
};

// dark -> teal -> amber -> hot for n in 0..1 (clamped), as 0.7 heatmapColor.
Rgba HeatmapColor(double n);

// Throws std::invalid_argument("funscript: heatmap needs at least 2 actions")
// with fewer than 2 actions.
HeatmapImage RenderHeatmapImage(const std::vector<Action>& actions, const HeatmapImageOptions& opts = {});

}  // namespace samn
