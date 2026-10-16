#include "samn/device/battery.h"

#include <cmath>

namespace samn::device {

std::string FormatBatteryPct(int pct) { return "Battery " + std::to_string(pct) + "%"; }

std::optional<int> BatteryFromGatt(const Bytes& value) {
    if (value.empty()) return std::nullopt;
    const int pct = value[0];
    return pct > 100 ? 100 : pct;
}

int BatteryFromButtplug(double level) {
    if (std::isnan(level)) return 0;
    // Go: int(raw*100 + 0.5), truncated toward zero, then clamped.
    const double scaled = level * 100 + 0.5;
    if (scaled <= 0) return 0;
    if (scaled >= 100) return 100;
    return static_cast<int>(scaled);
}

}  // namespace samn::device
