#include "samn/simplify.h"

#include <cmath>
#include <utility>

namespace samn {

std::vector<int> SimplifyIndices(const std::vector<CurvePoint>& pts, double max_error) {
    const int n = static_cast<int>(pts.size());
    std::vector<int> out;
    if (n <= 2) {
        for (int i = 0; i < n; ++i) out.push_back(i);
        return out;
    }
    if (max_error <= 0) max_error = 0.5;
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<int, int>> stack{{0, n - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b - a <= 1) continue;
        const CurvePoint p0 = pts[a], p1 = pts[b];
        const double dt = p1.t_ms - p0.t_ms;
        double max_dev = 0;
        int max_idx = -1;
        for (int i = a + 1; i < b; ++i) {
            // Equal or backwards times: compare against the start position.
            const double interp = dt <= 0 ? p0.pos : p0.pos + (p1.pos - p0.pos) * ((pts[i].t_ms - p0.t_ms) / dt);
            const double dev = std::abs(pts[i].pos - interp);
            if (dev > max_dev) {
                max_dev = dev;
                max_idx = i;
            }
        }
        if (max_idx >= 0 && max_dev > max_error) {
            keep[max_idx] = 1;
            stack.push_back({a, max_idx});
            stack.push_back({max_idx, b});
        }
    }
    for (int i = 0; i < n; ++i)
        if (keep[i]) out.push_back(i);
    return out;
}

}  // namespace samn
