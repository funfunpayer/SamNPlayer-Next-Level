#pragma once

// Tip ↔ partner gap for Tf/Tj distance profiles (P3.4 / GD-08).
// Full 2D hypot from the tip-box point nearest the partner centre to that
// centre. Overlapping boxes → 0. Small tips read like centre-to-centre;
// a large tip uses the near edge (not the tip mid).

#include <optional>
#include <vector>

#include <opencv2/core.hpp>

namespace samn::engine {

// Pixel distance. Empty tip or partner → 0 (caller holds the last good).
double TipPartnerDistance(const cv::Rect& tip, const cv::Rect& partner);

// The smallest TipPartnerDistance to any included partner (0.7
// FuseTipPartners, track_multi). nullopt when the tip is not usable or no
// partner is included; a partner without an include entry counts.
std::optional<double> FuseTipPartners(const cv::Rect& tip, bool tip_ok, const std::vector<cv::Rect>& partners,
                                      const std::vector<bool>& include);

}  // namespace samn::engine
