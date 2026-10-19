#include "samn/contact_marks.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

#include "samn/mapper.h"

namespace samn {

namespace {

bool Drawable(const ContactMarkBox& b) { return b.w > 0 && b.h > 0; }

}  // namespace

const std::vector<std::string>& TipClasses() {
    static const std::vector<std::string> kTip{"glans", "penis"};
    return kTip;
}

const std::vector<std::string>& ContactClasses() {
    static const std::vector<std::string> kContact{"mouth", "nipples", "breasts"};
    return kContact;
}

bool HasContactAreas(const ContactMarks& marks) {
    if (marks.primary && Drawable(*marks.primary)) return true;
    return std::any_of(marks.extras.begin(), marks.extras.end(), Drawable);
}

bool HasAnyArea(const ContactMarks& marks) { return (marks.tip && Drawable(*marks.tip)) || HasContactAreas(marks); }

double ProximityToBox(double x, double y, const ContactMarkBox& b, double margin) {
    if (!Drawable(b)) return 0;
    const double pad_x = static_cast<double>(b.w) * margin;
    const double pad_y = static_cast<double>(b.h) * margin;
    const double x0 = static_cast<double>(b.x), y0 = static_cast<double>(b.y);
    const double x1 = static_cast<double>(b.x + b.w), y1 = static_cast<double>(b.y + b.h);
    if (x < x0 - pad_x || x > x1 + pad_x || y < y0 - pad_y || y > y1 + pad_y) return 0;
    // Distance outside the core box (0 inside).
    const double dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0.0);
    const double dy = y < y0 ? y0 - y : (y > y1 ? y - y1 : 0.0);
    if (dx == 0 && dy == 0) return 1;
    if (pad_x <= 0 || pad_y <= 0) return 1;
    const double t = std::max(dx / pad_x, dy / pad_y);
    return t >= 1 ? 0 : 1 - t;
}

std::vector<TrajectoryPoint> SortedTipPath(const TrajectoryData& path) {
    std::vector<TrajectoryPoint> tip = path.tip;
    std::stable_sort(tip.begin(), tip.end(), [](const TrajectoryPoint& a, const TrajectoryPoint& b) { return a.at_ms < b.at_ms; });
    return tip;
}

std::optional<TrajectoryPoint> TipPathAt(const std::vector<TrajectoryPoint>& tip, int64_t at_ms) {
    if (tip.empty()) return std::nullopt;
    // The nearest sample; on a tie the first one (0.7 tipAtMs scanned for
    // the first minimum).
    const auto first_at = [&tip](int64_t t) {
        return std::lower_bound(tip.begin(), tip.end(), t, [](const TrajectoryPoint& p, int64_t v) { return p.at_ms < v; });
    };
    const auto it = first_at(at_ms);
    const TrajectoryPoint* best = nullptr;
    if (it == tip.end()) {
        best = &*first_at(tip.back().at_ms);
    } else if (it == tip.begin()) {
        best = &*it;
    } else {
        const auto before = first_at(std::prev(it)->at_ms);
        best = (at_ms - before->at_ms) <= (it->at_ms - at_ms) ? &*before : &*it;
    }
    if (std::llabs(best->at_ms - at_ms) > kTipPathMaxGapMs) return std::nullopt;
    return *best;
}

SpatialContact::SpatialContact(ContactMarks marks, const TrajectoryData& path)
    : marks_(std::move(marks)), tip_(SortedTipPath(path)) {}

double SpatialContact::At(int64_t at_ms) const {
    if (!HasContactAreas(marks_)) return 0;
    const auto p = TipPathAt(tip_, at_ms);
    if (!p) return 0;
    double out = 0;
    if (marks_.primary) out = std::max(out, ProximityToBox(p->x, p->y, *marks_.primary, kSpatialContactMargin));
    for (const ContactMarkBox& e : marks_.extras) out = std::max(out, ProximityToBox(p->x, p->y, e, kSpatialContactMargin));
    return out;
}

double SpatialContactIntensity(const ContactMarks* marks, const TrajectoryData* path, int64_t at_ms) {
    if (!marks || !path || !HasContactAreas(*marks)) return 0;
    return SpatialContact(*marks, *path).At(at_ms);
}

std::function<double(int64_t)> SpatialContactFor(const ScriptMetadata& metadata) {
    if (!metadata.contact_marks || !HasContactAreas(*metadata.contact_marks)) return {};
    if (!metadata.trajectory || metadata.trajectory->tip.empty()) return {};
    // Shared: MapOptions is copied freely, the path is not.
    auto spatial = std::make_shared<const SpatialContact>(*metadata.contact_marks, *metadata.trajectory);
    return [spatial](int64_t at_ms) { return spatial->At(at_ms); };
}

std::optional<ContactMarks> BuildContactMarks(const ContactMarkInput& in) {
    ContactMarks out;
    out.tip_class = in.tip_class;
    if (in.tip && Drawable(*in.tip)) {
        out.tip = *in.tip;
        out.tip->cls = in.tip_class;
        out.tip->fixed = false;
    }
    if (in.primary && Drawable(*in.primary)) out.primary = *in.primary;
    for (const ContactMarkBox& e : in.extras)
        if (Drawable(e)) out.extras.push_back(e);
    if (out.tip_class.empty() && !out.tip && !out.primary && out.extras.empty()) return std::nullopt;
    out.drive_stroke = IsDistanceProfile(in.profile);
    return out;
}

void ApplyContactVibration(DeviceRecipe& recipe, double span, const std::string& curve) {
    recipe.contact_vibration = true;
    if (span > 0) {
        const double s = EffectiveContactSpan(span);
        if (s != DefaultContactVibrationSpan) recipe.contact_vibration_span = s;
    }
    const std::string c = NormalizeContactCurve(curve);
    if (c != ContactCurveLinear) recipe.contact_vibration_curve = c;
}

}  // namespace samn
