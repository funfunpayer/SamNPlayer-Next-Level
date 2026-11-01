#pragma once
// Contact marks: the tip and the contact areas (mouth, nipples, breasts) the
// user marked in the Generator, and the contact vibration that follows the
// tip's path into those areas (0.7 funscript/contact_marks.go,
// funscript/feel_spatial.go, generator/contact_marks.go). The marks never
// change the stroke; only Tf/Tj (distance profiles) drive the stroke from a
// partner box.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "samn/samn_file.h"

namespace samn {

// Each contact box grows by this fraction of its size, so grazing still
// counts (0.7 SpatialContactMargin).
constexpr double kSpatialContactMargin = 0.25;
// A tip sample further than this from the asked time counts as lost.
constexpr int64_t kTipPathMaxGapMs = 250;

// The tip classes and contact classes the Generator offers (0.7 kept the
// lists minimal for vibration feel).
const std::vector<std::string>& TipClasses();      // glans, penis
const std::vector<std::string>& ContactClasses();  // mouth, nipples, breasts

// Any drawable box, the tip included (0.7 ContactMarks.HasAreas).
bool HasAnyArea(const ContactMarks& marks);
// A contact area (primary or extra); the tip box alone is not one.
bool HasContactAreas(const ContactMarks& marks);

// 1 inside the box, falling to 0 at the edge of the box grown by margin;
// 0 outside that and for an empty box.
double ProximityToBox(double x, double y, const ContactMarkBox& box, double margin);

// Tip / partner paths sorted by time (stable: of equal times the first stays
// first). Same shape so PathAt works on either.
std::vector<TrajectoryPoint> SortedTipPath(const TrajectoryData& path);
std::vector<TrajectoryPoint> SortedPartnerPath(const TrajectoryData& path);
// The sample nearest to at_ms (the first on a tie, 0.7 tipAtMs) in a sorted
// tip or partner path; nullopt when there is none within kTipPathMaxGapMs.
std::optional<TrajectoryPoint> PathAt(const std::vector<TrajectoryPoint>& sorted, int64_t at_ms);

// The tip path against the contact areas: 0..1 per video time. Built once
// (the path is sorted by time), asked per tick.
class SpatialContact {
public:
    SpatialContact(ContactMarks marks, const TrajectoryData& path);
    double At(int64_t at_ms) const;

private:
    ContactMarks marks_;
    std::vector<TrajectoryPoint> tip_;
};

// One call of the above (0.7 SpatialContactIntensity); 0 when the marks have
// no contact area or there is no path.
double SpatialContactIntensity(const ContactMarks* marks, const TrajectoryData* path, int64_t at_ms);

// The mapper's spatial_contact for a script: empty unless it has contact
// areas and a tip path.
std::function<double(int64_t)> SpatialContactFor(const ScriptMetadata& metadata);

// What the Generator stores (0.7 buildContactMarksMeta). nullopt when there
// is nothing to store. drive_stroke is set for Tf/Tj only.
struct ContactMarkInput {
    std::string profile;
    std::string tip_class;
    std::optional<ContactMarkBox> tip;
    std::optional<ContactMarkBox> primary;
    std::vector<ContactMarkBox> extras;
};
std::optional<ContactMarks> BuildContactMarks(const ContactMarkInput& input);

// 0.7 applyContactVibrationToRecipe: contact vibration on, span and curve
// only when they differ from the defaults.
void ApplyContactVibration(DeviceRecipe& recipe, double span, const std::string& curve);

}  // namespace samn
