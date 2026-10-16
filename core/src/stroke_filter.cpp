#include "samn/stroke_filter.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace samn {

namespace {

int clampPos(int p) {
    return std::max(0, std::min(100, p));
}

std::vector<Action> dedupeActionTimes(std::vector<Action> a) {
    if (a.size() < 2) return a;
    std::vector<Action> out;
    out.reserve(a.size());
    out.push_back(a[0]);
    for (size_t i = 1; i < a.size(); ++i) {
        if (a[i].at == out.back().at) {
            out.back() = a[i];
        } else {
            out.push_back(a[i]);
        }
    }
    return out;
}

const int64_t idleWaveMs = 1600;
const int idleWaveAmp = 12;
const int64_t idleMarginM = 400;

std::vector<Action> idleFill(const std::vector<Action>& a, int64_t minGap) {
    if (minGap < 2 * idleMarginM + idleWaveMs) {
        minGap = 2 * idleMarginM + idleWaveMs;
    }
    std::vector<Action> out;
    out.reserve(a.size() * 2);
    for (size_t i = 0; i < a.size(); ++i) {
        out.push_back(a[i]);
        if (i + 1 >= a.size()) break;
        int64_t start = a[i].at, end = a[i + 1].at;
        if (end - start < minGap) continue;
        int level = (a[i].pos + a[i + 1].pos) / 2;
        int lo = clampPos(level - idleWaveAmp);
        int hi = clampPos(level + idleWaveAmp);
        bool up = true;
        for (int64_t t = start + idleMarginM; t + idleWaveMs / 2 <= end - idleMarginM; t += idleWaveMs / 2) {
            int p = up ? hi : lo;
            up = !up;
            out.push_back({t, p});
        }
    }
    return out;
}

double hermite(double y0, double y1, double m0, double m1, double s) {
    double s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * y0 + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * y1 + (s3 - s2) * m1;
}

std::vector<double> secants(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    std::vector<double> delta(n - 1, 0.0);
    for (size_t i = 0; i + 1 < n; ++i) {
        double h = x[i + 1] - x[i];
        if (h > 0) delta[i] = (y[i + 1] - y[i]) / h;
    }
    return delta;
}

double pchipEnd(double h0, double h1, double d0, double d1) {
    double s = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    if ((s > 0) != (d0 > 0) || d0 == 0) return 0;
    if ((d0 > 0) != (d1 > 0) && std::abs(s) > std::abs(3 * d0)) return 3 * d0;
    return s;
}

std::vector<double> pchipSlopes(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    std::vector<double> d(n, 0.0);
    auto delta = secants(x, y);
    if (n == 2) {
        d[0] = d[1] = delta[0];
        return d;
    }
    for (size_t i = 1; i + 1 < n; ++i) {
        double a = delta[i - 1], b = delta[i];
        if (a == 0 || b == 0 || (a > 0) != (b > 0)) continue;
        double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
        double w1 = 2 * h1 + h0, w2 = h1 + 2 * h0;
        d[i] = (w1 + w2) / (w1 / a + w2 / b);
    }
    d[0] = pchipEnd(x[1] - x[0], x[2] - x[1], delta[0], delta[1]);
    d[n - 1] = pchipEnd(x[n - 1] - x[n - 2], x[n - 2] - x[n - 3], delta[n - 2], delta[n - 3]);
    return d;
}

std::vector<double> makimaSlopes(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    auto delta = secants(x, y);
    std::vector<double> m(n + 3, 0.0);
    for (size_t i = 0; i + 1 < n; ++i) {
        m[i + 2] = delta[i];
    }
    if (n - 1 >= 2) {
        m[1] = 2 * m[2] - m[3];
        m[0] = 2 * m[1] - m[2];
        m[n + 1] = 2 * m[n] - m[n - 1];
        m[n + 2] = 2 * m[n + 1] - m[n];
    } else {
        m[0] = m[1] = m[n + 1] = m[n + 2] = m[2];
    }
    std::vector<double> d(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double m0 = m[i], m1 = m[i + 1], m2 = m[i + 2], m3 = m[i + 3];
        double w1 = std::abs(m3 - m2) + std::abs(m3 + m2) / 2;
        double w2 = std::abs(m1 - m0) + std::abs(m1 + m0) / 2;
        if (w1 + w2 == 0) {
            d[i] = 0;
            continue;
        }
        d[i] = (w1 * m1 + w2 * m2) / (w1 + w2);
    }
    return d;
}

std::vector<Action> smoothResample(const std::vector<Action>& a, const std::string& mode, int64_t step) {
    size_t n = a.size();
    std::vector<double> x(n), y(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<double>(a[i].at);
        y[i] = static_cast<double>(a[i].pos);
    }
    auto d = (mode == "makima") ? makimaSlopes(x, y) : pchipSlopes(x, y);
    std::vector<Action> out;
    out.reserve(n * 2);
    for (size_t i = 0; i + 1 < n; ++i) {
        out.push_back(a[i]);
        double h = x[i + 1] - x[i];
        if (h <= 0) continue;
        for (int64_t t = a[i].at + step; t < a[i + 1].at; t += step) {
            double s = (static_cast<double>(t) - x[i]) / h;
            double v = hermite(y[i], y[i + 1], d[i] * h, d[i + 1] * h, s);
            out.push_back({t, clampPos(static_cast<int>(std::round(v)))});
        }
    }
    out.push_back(a.back());
    return out;
}

std::vector<Action> limitSpeed(std::vector<Action> a, double maxSpeed) {
    for (size_t i = 1; i < a.size(); ++i) {
        double dt = static_cast<double>(a[i].at - a[i - 1].at) / 1000.0;
        if (dt <= 0) {
            a[i].pos = a[i - 1].pos;
            continue;
        }
        double maxStep = maxSpeed * dt;
        double diff = static_cast<double>(a[i].pos - a[i - 1].pos);
        if (std::abs(diff) > maxStep) {
            if (diff > 0) {
                a[i].pos = a[i - 1].pos + static_cast<int>(std::floor(maxStep));
            } else {
                a[i].pos = a[i - 1].pos - static_cast<int>(std::floor(maxStep));
            }
        }
    }
    return a;
}

std::string NormalizeSmooth(const std::string& s) {
    if (s == "pchip" || s == "makima") return s;
    return "";
}

}  // namespace

bool Active(const StrokeFilter& f) {
    return !f.smooth.empty() || f.maxSpeed > 0 || f.idleFillMs > 0;
}

std::vector<Action> ApplyStrokeFilter(std::vector<Action> actions, const StrokeFilter& f) {
    if (!Active(f) || actions.size() < 2) return actions;
    std::stable_sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
    actions = dedupeActionTimes(std::move(actions));
    if (actions.size() < 2) return actions;
    if (f.idleFillMs > 0) {
        actions = idleFill(actions, f.idleFillMs);
    }
    std::string s = NormalizeSmooth(f.smooth);
    if (!s.empty()) {
        int64_t step = f.stepMs > 0 ? f.stepMs : 100;
        actions = smoothResample(actions, s, step);
    }
    if (f.maxSpeed > 0) {
        actions = limitSpeed(std::move(actions), f.maxSpeed);
    }
    return actions;
}

}  // namespace samn
