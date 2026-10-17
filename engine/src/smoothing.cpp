#include <algorithm>
#include <cmath>

#include "track_parts.h"

namespace samn::engine {

namespace {

// Least squares A^T A c = A^T y with Gauss elimination and column pivoting;
// the system is tiny (polyorder + 1).
std::vector<double> PolyFit(const double* y, int n, int order) {
    const int terms = order + 1;
    std::vector<std::vector<double>> m(terms, std::vector<double>(terms, 0.0));
    std::vector<double> rhs(terms, 0.0), pw(terms);
    for (int xi = 0; xi < n; ++xi) {
        double p = 1.0;
        for (int k = 0; k < terms; ++k, p *= xi) pw[k] = p;
        for (int r = 0; r < terms; ++r) {
            rhs[r] += pw[r] * y[xi];
            for (int c = 0; c < terms; ++c) m[r][c] += pw[r] * pw[c];
        }
    }
    for (int col = 0; col < terms; ++col) {
        int pivot = col;
        for (int r = col + 1; r < terms; ++r)
            if (std::abs(m[r][col]) > std::abs(m[pivot][col])) pivot = r;
        std::swap(m[col], m[pivot]);
        std::swap(rhs[col], rhs[pivot]);
        if (std::abs(m[col][col]) < 1e-12) continue;
        for (int r = col + 1; r < terms; ++r) {
            const double f = m[r][col] / m[col][col];
            for (int c = col; c < terms; ++c) m[r][c] -= f * m[col][c];
            rhs[r] -= f * rhs[col];
        }
    }
    std::vector<double> x(terms, 0.0);
    for (int i = terms - 1; i >= 0; --i) {
        double sum = rhs[i];
        for (int j = i + 1; j < terms; ++j) sum -= m[i][j] * x[j];
        x[i] = std::abs(m[i][i]) < 1e-12 ? 0.0 : sum / m[i][i];
    }
    return x;
}

}  // namespace

std::vector<double> Savgol(const std::vector<double>& y, int window, int polyorder) {
    const int n = static_cast<int>(y.size());
    if (n < window || window < 1) return y;
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        // The window stays whole at the edges (shifted, not mirrored): scipy "interp".
        const int start = std::clamp(i - window / 2, 0, n - window);
        const std::vector<double> c = PolyFit(y.data() + start, window, polyorder);
        double v = 0, p = 1, x = i - start;
        for (double k : c) {
            v += k * p;
            p *= x;
        }
        out[i] = v;
    }
    return out;
}

void ApplyCameraCompensation(std::vector<double>& ys, std::vector<double> dy, const std::vector<int>& cuts) {
    // Per scene, so smoothing never mixes drift across a cut that reset it.
    std::vector<int> bounds{0};
    bounds.insert(bounds.end(), cuts.begin(), cuts.end());
    bounds.push_back(static_cast<int>(dy.size()));
    for (size_t i = 0; i + 1 < bounds.size(); ++i) {
        const int a = bounds[i], b = bounds[i + 1];
        if (b - a < 9) continue;
        const std::vector<double> s = Savgol(std::vector<double>(dy.begin() + a, dy.begin() + b), 9, 2);
        std::copy(s.begin(), s.end(), dy.begin() + a);
    }
    for (size_t i = 0; i < ys.size() && i < dy.size(); ++i) ys[i] -= dy[i];
}

}  // namespace samn::engine
