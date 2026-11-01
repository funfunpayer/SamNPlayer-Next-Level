#pragma once
// Live Feel probe (0.7 PreviewContactVibration): a fixed synthetic bounce
// stroke through the contact-vibration depth envelope so Sensitivity /
// Curve show feedback before Create.
#include <cstdint>
#include <string>
#include <vector>

#include "samn/funscript.h"

namespace samn {

struct ContactVibrationPreviewPoint {
    double x = 0;  // 0..1 along the probe
    double y = 0;  // 0..1 (stroke pos/100 or vib)
};

struct ContactVibrationPreview {
    double active_pct = 0;  // share of ticks with vib > 0, as 0..100
    double peak = 0;        // max vib as 0..100
    std::string hint;
    std::vector<ContactVibrationPreviewPoint> stroke;
    std::vector<ContactVibrationPreviewPoint> vib;
    // Local maxima of vib with value >= 0.15 (0.7 peak dots).
    std::vector<ContactVibrationPreviewPoint> peaks;
};

// The fixed bounce used by the probe (also useful in tests).
std::vector<Action> SyntheticContactBounce();

// span 0.4..0.95 (EffectiveContactSpan), curve linear/soft/peak/impulse,
// strength multiplies vib (0..2, default 1). Maps with SuctionPosition so the
// plot is the contact depth envelope only (Sensitivity / Curve stay clear).
ContactVibrationPreview PreviewContactVibration(double span, const std::string& curve,
                                                double strength = 1.0);

}  // namespace samn
