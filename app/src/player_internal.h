#pragma once
// Shared helpers for PlayerController translation units (not a public API).
#include <QLatin1String>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"

namespace samn::ui::player_internal {

enum class EditLane { Stroke, Vibration, Suction };

inline std::optional<EditLane> ParseEditLane(const QString& lane, bool vibFromScript, bool sucFromScript) {
    if (lane == QLatin1String("stroke")) return EditLane::Stroke;
    if (lane == QLatin1String("vibration") && vibFromScript) return EditLane::Vibration;
    if (lane == QLatin1String("suction") && sucFromScript) return EditLane::Suction;
    return std::nullopt;
}

inline std::vector<samn::Action> LaneFrom(const std::vector<samn::Frame>& frames, bool suction) {
    std::vector<samn::Action> out;
    out.reserve(frames.size());
    for (const auto& f : frames)
        out.push_back({f.at, static_cast<int>(std::lround((suction ? f.suction : f.vibration) * 100))});
    return out;
}

// Median spacing of stroke peaks → BPM in 20–240 (0 when unclear).
inline double EstimateStrokeBpm(const std::vector<samn::Action>& stroke) {
    if (stroke.size() < 4) return 0;
    std::vector<int64_t> peaks;
    peaks.reserve(stroke.size() / 2);
    for (size_t i = 1; i + 1 < stroke.size(); ++i) {
        const int p = stroke[i].pos;
        if (p < 55) continue;
        if (p >= stroke[i - 1].pos && p > stroke[i + 1].pos) peaks.push_back(stroke[i].at);
    }
    if (peaks.size() < 2) return 0;
    std::vector<int64_t> gaps;
    gaps.reserve(peaks.size() - 1);
    for (size_t i = 1; i < peaks.size(); ++i) {
        const int64_t g = peaks[i] - peaks[i - 1];
        if (g >= 250 && g <= 3000) gaps.push_back(g);
    }
    if (gaps.empty()) return 0;
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const double period = double(gaps[gaps.size() / 2]);
    if (period <= 0) return 0;
    return std::clamp(std::round(60000.0 / period), 20.0, 240.0);
}

inline QString Secs(int64_t ms) { return QString::number(ms / 1000.0, 'f', 1) + QLatin1Char('s'); }

inline QString CleanName(const QString& name, const QString& fallback) {
    const QString n = name.trimmed().left(80);
    return n.isEmpty() ? fallback : n;
}

}  // namespace samn::ui::player_internal
