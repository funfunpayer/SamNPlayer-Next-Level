#pragma once

// Tip ↔ partner gap for Tf/Tj distance profiles (P3.4 / GD-08).
// Full 2D hypot from the tip-box point nearest the partner centre to that
// centre. Overlapping boxes → 0. Small tips read like centre-to-centre;
// a large tip uses the near edge (not the tip mid).

#include <opencv2/core.hpp>

namespace samn::engine {

// Pixel distance. Empty tip or partner → 0 (caller holds the last good).
double TipPartnerDistance(const cv::Rect& tip, const cv::Rect& partner);

}  // namespace samn::engine
