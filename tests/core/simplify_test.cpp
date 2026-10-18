#include <doctest/doctest.h>

#include <cmath>

#include "samn/simplify.h"

using samn::SimplifyPoint;
using samn::SimplifyIndices;

namespace {
std::vector<SimplifyPoint> Sine(int n, double fps, double freq) {
    std::vector<SimplifyPoint> out(n);
    for (int i = 0; i < n; ++i) {
        const double t = i / fps;
        out[i] = {t * 1000, (std::sin(2 * 3.14159265358979323846 * freq * t) + 1) / 2 * 100};
    }
    return out;
}
double Interpolate(const std::vector<SimplifyPoint>& kept, double t) {
    for (size_t i = 1; i < kept.size(); ++i)
        if (t <= kept[i].t_ms) {
            const SimplifyPoint a = kept[i - 1], b = kept[i];
            return a.pos + (b.pos - a.pos) * (t - a.t_ms) / (b.t_ms - a.t_ms);
        }
    return kept.back().pos;
}
}  // namespace

// Ported from 0.7 motionx/motionx_test.go (Simplify cases).
TEST_CASE("simplify: keeps the endpoints") {
    const auto pts = Sine(200, 30, 1);
    const auto idx = SimplifyIndices(pts, 1.0);
    REQUIRE(idx.size() >= 2);
    CHECK(idx.front() == 0);
    CHECK(idx.back() == 199);
}

TEST_CASE("simplify: every sample stays within the tolerance") {
    const auto pts = Sine(300, 30, 1.2);
    std::vector<SimplifyPoint> kept;
    for (int i : SimplifyIndices(pts, 2.0)) kept.push_back(pts[i]);
    CHECK(kept.size() < pts.size());
    for (const SimplifyPoint& p : pts) CHECK(std::abs(Interpolate(kept, p.t_ms) - p.pos) <= 2.0 + 1e-6);
}

TEST_CASE("simplify: a straight line collapses to two points") {
    std::vector<SimplifyPoint> pts(500);
    for (int i = 0; i < 500; ++i) pts[i] = {i * 10.0, i * 0.1};
    CHECK(SimplifyIndices(pts, 0.5).size() == 2);
}

TEST_CASE("simplify: a deep sawtooth does not recurse") {
    std::vector<SimplifyPoint> pts(20000);
    for (int i = 0; i < 20000; ++i) pts[i] = {i * 10.0, i % 2 ? 100.0 : 0.0};
    CHECK(SimplifyIndices(pts, 0.5).size() >= 1000);
}
