#include "samn/script_quality.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace samn {
namespace {

double Round1(double v) { return std::round(v * 10) / 10; }

double Median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t mid = v.size() / 2;
    return v.size() % 2 == 0 ? (v[mid - 1] + v[mid]) / 2 : v[mid];
}

double MedianAbsDev(const std::vector<double>& v, double med) {
    std::vector<double> d;
    d.reserve(v.size());
    for (double x : v) d.push_back(std::abs(x - med));
    return Median(std::move(d));
}

// Linear interpolation between closest ranks (0.7 percentileFloat).
double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    if (p <= 0) return v.front();
    if (p >= 100) return v.back();
    const double rank = p / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(std::floor(rank)), hi = size_t(std::ceil(rank));
    if (lo == hi) return v[lo];
    const double w = rank - double(lo);
    return v[lo] * (1 - w) + v[hi] * w;
}

template <typename... A>
std::string Fmt(const char* f, A... a) {
    char buf[512];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

}  // namespace

DeviceCompat EvaluateDeviceCompat(const std::vector<Action>& actions) {
    DeviceCompat out;
    auto& m = out.metrics;
    if (actions.size() < 2) return out;
    const size_t n = actions.size() - 1;
    std::vector<double> dt(n), dpos(n), intens(n, 0.0);
    double weight = 0, intensSum = 0;
    double lo = actions[0].pos, hi = actions[0].pos;
    for (size_t i = 0; i < n; ++i) {
        dt[i] = double(actions[i + 1].at - actions[i].at);
        dpos[i] = std::abs(double(actions[i + 1].pos - actions[i].pos));
        if (dt[i] > 0) {
            intens[i] = 500.0 * dpos[i] / dt[i];
            intensSum += intens[i] * dt[i];
            weight += dt[i];
        }
        lo = std::min({lo, double(actions[i].pos), double(actions[i + 1].pos)});
        hi = std::max({hi, double(actions[i].pos), double(actions[i + 1].pos)});
    }
    if (weight > 0) m.avg_intensity = Round1(intensSum / weight);
    m.peak_intensity = Round1(Percentile(intens, 95));

    for (double d : dt)
        if (d > 0 && d < kMinActionIntervalMs) ++m.actions_too_close;
    m.too_close_share = double(m.actions_too_close) / double(n);
    if (m.actions_too_close > 0 && m.too_close_share > 0.05)
        out.warnings.push_back(Fmt(
            "%d actions (%.0f%%) follow each other faster than %.0f ms - the device cannot play them one by one, "
            "so these parts fall out of step",
            m.actions_too_close, m.too_close_share * 100, kMinActionIntervalMs));

    int large = 0;
    for (size_t i = 0; i < n; ++i) {
        if (dpos[i] >= 50) {
            ++large;
            if (dt[i] > kSlowestFullStrokeMs) ++m.slow_full_strokes;
        }
    }
    if (m.slow_full_strokes > 0 && large > 0 && double(m.slow_full_strokes) / large > 0.25)
        out.warnings.push_back(Fmt(
            "%d large moves take longer than %.0f ms - the device moves faster and then waits, which makes unwanted pauses",
            m.slow_full_strokes, kSlowestFullStrokeMs));

    m.position_span = Round1(hi - lo);
    if (hi - lo < 40)
        out.warnings.push_back(
            Fmt("The script only uses the range %.0f-%.0f of 100 - the motion on the device stays weak", lo, hi));

    for (const auto& a : actions)
        if (a.pos < kUsablePositionMin || a.pos > kUsablePositionMax) ++m.actions_outside_usable_range;
    return out;
}

ScriptQualityResult EvaluateScriptQuality(const std::vector<Action>& actions) {
    ScriptQualityResult r;
    if (actions.size() < 2) {
        r.warnings.push_back("Fewer than 2 actions - no usable script");
        return r;
    }
    double penalty = 0;
    for (size_t i = 1; i < actions.size(); ++i) {
        if (actions[i].at < actions[i - 1].at) {
            r.warnings.push_back("Timestamps are not in ascending order");
            penalty += 0.3;
            break;
        }
    }
    std::vector<Action> sorted = actions;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Action& a, const Action& b) { return a.at < b.at; });

    int dup = 0, oor = 0;
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i].at == sorted[i - 1].at) ++dup;
    if (dup > 0) {
        r.warnings.push_back(Fmt("%d duplicate timestamps", dup));
        penalty += std::min(0.2, dup * 0.02);
    }
    for (const auto& a : sorted)
        if (a.pos < 0 || a.pos > 100) ++oor;
    if (oor > 0) {
        r.warnings.push_back(Fmt("%d position values outside 0-100", oor));
        penalty += std::min(0.3, oor * 0.05);
    }

    std::vector<double> dts;
    for (size_t i = 1; i < sorted.size(); ++i) {
        const double d = double(sorted[i].at - sorted[i - 1].at);
        if (d > 0) dts.push_back(d);
    }
    if (!dts.empty()) {
        const double thresh = std::max(Median(dts) * 8, 2000.0);
        int gaps = 0;
        for (double d : dts)
            if (d > thresh) ++gaps;
        if (gaps > 0) {
            r.warnings.push_back(Fmt("%d unusually large time gap(s) (>%dms)", gaps, int(thresh)));
            penalty += std::min(0.25, gaps * 0.08);
        }
    }
    if (dts.size() > 4) {
        std::vector<double> speeds;
        for (size_t i = 1; i < sorted.size(); ++i) {
            const double d = double(sorted[i].at - sorted[i - 1].at);
            if (d <= 0) continue;
            speeds.push_back(std::abs(double(sorted[i].pos - sorted[i - 1].pos)) / std::max(d, 1.0));
        }
        if (speeds.size() > 4) {
            const double med = Median(speeds);
            const double mad = MedianAbsDev(speeds, med) + 1e-6;
            const double spike = std::max(med + 10 * mad, 0.5);
            int spikes = 0;
            for (double s : speeds)
                if (s > spike) ++spikes;
            if (spikes > 0) {
                r.warnings.push_back(Fmt("%d extreme speed spike(s) - points to a tracking jump", spikes));
                penalty += std::min(0.3, spikes * 0.05);
            }
        }
    }
    double score = std::max(0.0, 1.0 - penalty);
    const DeviceCompat dev = EvaluateDeviceCompat(sorted);
    r.warnings.insert(r.warnings.end(), dev.warnings.begin(), dev.warnings.end());
    score -= std::min(0.2, 0.1 * double(dev.warnings.size()));
    r.score = std::max(0.0, score);
    r.passed = r.score >= 0.5;
    return r;
}

ScriptQualityResult ScriptQualityOfFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("script could not be read: " + path.u8string());
    std::stringstream ss;
    ss << in.rdbuf();
    nlohmann::json doc = nlohmann::json::parse(ss.str(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) throw std::runtime_error("invalid JSON");
    auto read = [&](const char* key) {
        std::vector<Action> out;
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_array()) return out;
        for (const auto& a : *it) {
            if (!a.is_object()) continue;
            Action x;
            if (auto t = a.find("at"); t != a.end() && t->is_number()) x.at = int64_t(t->get<double>());
            if (auto p = a.find("pos"); p != a.end() && p->is_number()) x.pos = int(p->get<double>());
            out.push_back(x);
        }
        return out;
    };
    std::vector<Action> actions = read("actions");
    if (actions.empty()) actions = read("general");
    if (actions.empty()) throw std::runtime_error("no actions found in script");
    return EvaluateScriptQuality(actions);
}

}  // namespace samn
