#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>

#include "samn/heatmap_image.h"

using samn::Action;
using samn::Chapter;
using samn::HeatmapColor;
using samn::HeatmapImage;
using samn::HeatmapImageOptions;
using samn::RenderHeatmapImage;
using samn::Rgba;

namespace {

// FNV-1a 64 over the RGBA bytes (the Go side hashed the decoded PNG the same way).
uint64_t Fnv(const HeatmapImage& img) {
    uint64_t h = 14695981039346656037ULL;
    for (uint8_t b : img.rgba) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace

// Go: TestExportHeatmapPNG. The pixel values below were produced by Go
// ExportHeatmapPNG and decoded with image/png from the same inputs.
TEST_CASE("TestExportHeatmapPNG") {
    std::vector<Action> actions{{0, 0}, {100, 100}, {500, 20}, {900, 80}};
    HeatmapImageOptions opts;
    opts.width = 120;
    opts.height = 24;
    opts.chapters = {Chapter{"A", 100, 0}};
    HeatmapImage img = RenderHeatmapImage(actions, opts);
    CHECK(img.width == 120);
    CHECK(img.height == 24);
    CHECK(img.rgba.size() == 120u * 24u * 4u);
    CHECK(Fnv(img) == 14343805365354933103ULL);
    CHECK(img.At(6, 22) == Rgba{243, 97, 40, 255});
    CHECK(img.At(13, 0) == Rgba{47, 212, 196, 255});  // chapter tick, 100*120/900
    CHECK(img.At(13, 22) == Rgba{47, 212, 196, 255});
    CHECK(img.At(40, 22) == Rgba{35, 138, 137, 255});
    CHECK(img.At(93, 22) == Rgba{31, 113, 118, 255});
    CHECK(img.At(0, 0) == Rgba{12, 16, 24, 255});  // background
}

TEST_CASE("Heatmap pixels equal Go on a 400-action script") {
    // Same LCG as the Go dump: 400 actions, 64 x 20, five chapter starts (two
    // outside the script, one just inside).
    std::vector<Action> actions;
    uint32_t seed = 12345;
    int64_t at = 500;
    for (int i = 0; i < 400; ++i) {
        seed = seed * 1664525u + 1013904223u;
        at += 40 + (seed >> 16) % 600;
        seed = seed * 1664525u + 1013904223u;
        actions.push_back({at, static_cast<int>((seed >> 16) % 101)});
    }
    REQUIRE(actions[0].at == 677);
    REQUIRE(actions[0].pos == 74);
    REQUIRE(actions[399].at == 133207);
    HeatmapImageOptions opts;
    opts.width = 64;
    opts.height = 20;
    opts.chapters = {Chapter{"", 0, 0}, Chapter{"", actions[100].at, 0}, Chapter{"", actions[399].at, 0},
                     Chapter{"", actions[399].at + 5, 0}, Chapter{"", 3, 0}};
    HeatmapImage img = RenderHeatmapImage(actions, opts);
    CHECK(Fnv(img) == 16754415951752513124ULL);
    CHECK(img.At(0, 0) == Rgba{47, 212, 196, 255});  // tick at the first action
    CHECK(img.At(1, 18) == Rgba{113, 200, 150, 255});
    CHECK(img.At(2, 18) == Rgba{243, 103, 41, 255});
    CHECK(img.At(3, 18) == Rgba{59, 209, 186, 255});
    CHECK(img.At(4, 18) == Rgba{38, 158, 153, 255});
    CHECK(img.At(5, 18) == Rgba{231, 179, 67, 255});
    CHECK(img.At(6, 18) == Rgba{25, 73, 86, 255});
    CHECK(img.At(10, 18) == Rgba{44, 197, 184, 255});
}

TEST_CASE("Heatmap defaults, errors and colour ramp") {
    CHECK_THROWS_WITH_AS(RenderHeatmapImage({{0, 0}}), "funscript: heatmap needs at least 2 actions",
                         std::invalid_argument);
    HeatmapImage d = RenderHeatmapImage({{0, 0}, {1000, 100}});
    CHECK(d.width == 800);
    CHECK(d.height == 48);
    // Colour stops (Go heatmapColor): dark, teal, amber, hot; input is clamped.
    CHECK(HeatmapColor(-1) == Rgba{20, 40, 60, 255});
    CHECK(HeatmapColor(0.35) == Rgba{47, 212, 196, 255});
    CHECK(HeatmapColor(0.7) == Rgba{243, 178, 60, 255});
    // n = 1: t = 0.9999999999999999, so truncation gives G 97 (Go prints 243 97 40).
    CHECK(HeatmapColor(1) == Rgba{243, 97, 40, 255});
    CHECK(HeatmapColor(7) == Rgba{243, 97, 40, 255});
    // The one-bucket image Go writes for [(0,0),(100,100)] has this colour at (0, h-2).
    HeatmapImageOptions one;
    one.width = 1;
    one.height = 10;
    CHECK(RenderHeatmapImage({{0, 0}, {100, 100}}, one).At(0, 8) == Rgba{243, 97, 40, 255});
}
