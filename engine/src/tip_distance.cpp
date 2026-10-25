#include "samn/engine/tip_distance.h"

#include <algorithm>
#include <cmath>

namespace samn::engine {

namespace {

bool Overlaps(const cv::Rect& a, const cv::Rect& b) {
    return (a & b).area() > 0;
}

cv::Point2d Centre(const cv::Rect& r) { return {r.x + r.width / 2.0, r.y + r.height / 2.0}; }

// Nearest point on the closed tip rectangle to p (clamp onto the box).
cv::Point2d NearestOnTip(const cv::Rect& tip, const cv::Point2d& p) {
    const double x1 = tip.x;
    const double y1 = tip.y;
    const double x2 = tip.x + tip.width;
    const double y2 = tip.y + tip.height;
    return {std::clamp(p.x, x1, x2), std::clamp(p.y, y1, y2)};
}

}  // namespace

double TipPartnerDistance(const cv::Rect& tip, const cv::Rect& partner) {
    if (tip.width <= 0 || tip.height <= 0 || partner.width <= 0 || partner.height <= 0) return 0;
    if (Overlaps(tip, partner)) return 0;
    const cv::Point2d partner_pt = Centre(partner);
    const cv::Point2d tip_pt = NearestOnTip(tip, partner_pt);
    return std::hypot(tip_pt.x - partner_pt.x, tip_pt.y - partner_pt.y);
}

}  // namespace samn::engine
