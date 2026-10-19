// Port of 0.7 funscript/feel_spatial_test.go, feel_spatial_mapper_test.go and
// generator/contact_marks_test.go.
#include <doctest/doctest.h>

#include <cmath>

#include "samn/contact_marks.h"
#include "samn/mapper.h"
#include "samn/samn_file.h"

using namespace samn;

namespace {

ContactMarkBox Box(int64_t x, int64_t y, int64_t w, int64_t h, std::string cls = "", bool fixed = false) {
    ContactMarkBox b;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.cls = std::move(cls);
    b.fixed = fixed;
    return b;
}

TrajectoryData Path(std::vector<TrajectoryPoint> tip) {
    TrajectoryData t;
    t.width = 200;
    t.height = 200;
    t.tip = std::move(tip);
    return t;
}

ScriptMetadata WithMarks(std::optional<ContactMarkBox> primary, TrajectoryData path) {
    ScriptMetadata m;
    ContactMarks cm;
    cm.primary = std::move(primary);
    m.contact_marks = cm;
    m.trajectory = std::move(path);
    return m;
}

MapOptions SpatialOptions(const ScriptMetadata& meta) {
    MapOptions o = DefaultMapOptions();
    o.tick_ms = 50;
    o.contact_vibration = true;
    o.contact_vibration_span = 0.75;
    o.contact_vibration_envelope = -1;
    o.sync = SyncMode::Independent;
    o.spatial_contact = SpatialContactFor(meta);
    return o;
}

double VibAt(const std::vector<Action>& actions, const MapOptions& o, int64_t at) {
    const auto frames = ToIntensityCurve(actions, o);
    REQUIRE(frames);
    for (const Frame& f : *frames)
        if (f.at == at) return f.vibration;
    FAIL("no frame at " << at);
    return 0;
}

}  // namespace

TEST_CASE("contact proximity: inside the core box is 1, far away 0, the margin falls off") {
    const ContactMarkBox b = Box(100, 100, 40, 40);
    CHECK(ProximityToBox(120, 120, b, kSpatialContactMargin) == 1);
    CHECK(ProximityToBox(10, 10, b, kSpatialContactMargin) == 0);
    // Half way into the 10 px margin on the right.
    CHECK(ProximityToBox(145, 120, b, kSpatialContactMargin) == doctest::Approx(0.5));
    CHECK(ProximityToBox(120, 120, Box(100, 100, 0, 40), kSpatialContactMargin) == 0);
}

TEST_CASE("spatial contact follows the tip path through a contact area") {
    ContactMarks marks;
    marks.primary = Box(50, 50, 20, 20, "nipples");
    const TrajectoryData path = Path({{0, 10, 10}, {1000, 60, 60}, {2000, 10, 10}});
    CHECK(SpatialContactIntensity(&marks, &path, 0) == 0);
    CHECK(SpatialContactIntensity(&marks, &path, 1000) >= 0.99);
    CHECK(SpatialContactIntensity(&marks, &path, 2000) == 0);
    // Nearest sample within 250 ms; beyond that the path counts as lost.
    CHECK(SpatialContactIntensity(&marks, &path, 1200) >= 0.99);
    CHECK(SpatialContactIntensity(&marks, &path, 2300) == 0);
    const TrajectoryData sparse = Path({{0, 60, 60}});
    CHECK(SpatialContactIntensity(&marks, &sparse, 251) == 0);
    CHECK(SpatialContactIntensity(&marks, &sparse, 250) >= 0.99);
}

TEST_CASE("spatial contact ignores a tip box alone and missing data") {
    ContactMarks tip_only;
    tip_only.tip = Box(0, 0, 50, 50);
    const TrajectoryData path = Path({{0, 10, 10}});
    CHECK(SpatialContactIntensity(&tip_only, &path, 0) == 0);
    CHECK(SpatialContactIntensity(nullptr, &path, 0) == 0);
    CHECK(SpatialContactIntensity(&tip_only, nullptr, 0) == 0);
    CHECK_FALSE(HasContactAreas(tip_only));
    CHECK(HasAnyArea(tip_only));
    CHECK_FALSE(SpatialContactFor(WithMarks(std::nullopt, path)));
    CHECK_FALSE(SpatialContactFor(WithMarks(Box(0, 0, 10, 10), Path({}))));
}

TEST_CASE("spatial contact takes the nearest sample, the first on a tie, in any order") {
    ContactMarks marks;
    marks.primary = Box(50, 50, 20, 20);
    // Out of order and with a tie at 500 ms between 400 and 600.
    const TrajectoryData path = Path({{600, 0, 0}, {0, 0, 0}, {400, 60, 60}, {400, 0, 0}});
    CHECK(SpatialContactIntensity(&marks, &path, 500) >= 0.99);  // 400 (first) wins the tie
    CHECK(SpatialContactIntensity(&marks, &path, 560) == 0);     // 600 is nearer
    CHECK(SpatialContactIntensity(&marks, &path, 400) >= 0.99);
}

TEST_CASE("an extra contact area counts the same as the primary") {
    ContactMarks marks;
    marks.primary = Box(0, 0, 10, 10);
    marks.extras.push_back(Box(100, 100, 20, 20, "nipples", true));
    const TrajectoryData path = Path({{0, 110, 110}});
    CHECK(SpatialContactIntensity(&marks, &path, 0) >= 0.99);
}

TEST_CASE("feel: a shallow stroke with the tip on a contact mark vibrates") {
    const std::vector<Action> actions{{0, 20}, {1000, 30}, {2000, 20}};
    const MapOptions o = SpatialOptions(WithMarks(Box(40, 40, 40, 40), Path({{0, 0, 0}, {1000, 60, 60}, {2000, 0, 0}})));
    const double mid = VibAt(actions, o, 1000);
    const double edge = VibAt(actions, o, 0);
    CHECK(mid >= 0.5);
    CHECK(edge <= mid);
}

TEST_CASE("feel: with marks, a deep stroke away from them is attenuated to the depth weight") {
    const std::vector<Action> actions{{0, 20}, {500, 90}, {1000, 20}};
    MapOptions depth = SpatialOptions(ScriptMetadata{});
    depth.min_vibration = 0;
    MapOptions spatial = SpatialOptions(WithMarks(Box(40, 40, 40, 40), Path({{0, 0, 0}, {500, 0, 0}, {1000, 0, 0}})));
    spatial.min_vibration = 0;
    const double full = VibAt(actions, depth, 500);
    const double prefer = VibAt(actions, spatial, 500);
    CHECK(full >= 0.8);
    CHECK(std::abs(prefer - full * SpatialDepthWeight) <= 0.05);
}

TEST_CASE("feel: a spatial hit keeps its full strength") {
    const std::vector<Action> actions{{0, 20}, {1000, 25}, {2000, 20}};
    MapOptions o = SpatialOptions(WithMarks(Box(40, 40, 40, 40), Path({{0, 0, 0}, {1000, 60, 60}, {2000, 0, 0}})));
    o.min_vibration = 0;
    CHECK(VibAt(actions, o, 1000) >= 0.9);
}

TEST_CASE("feel: a spatial hit uses the snappier envelope") {
    const std::vector<Action> actions{{0, 20}, {2000, 20}};
    MapOptions o = SpatialOptions(
        WithMarks(Box(40, 40, 40, 40), Path({{0, 0, 0}, {1000, 60, 60}, {1050, 0, 0}, {2000, 0, 0}})));
    o.contact_vibration_envelope = DefaultContactEnvelopeSmooth;
    o.min_vibration = 0;
    o.smoothing = 0;
    const double peak = VibAt(actions, o, 1000);
    const double after = VibAt(actions, o, 1200);
    CHECK(peak >= 0.5);
    CHECK(after <= peak * 0.55);
}

TEST_CASE("feel: contact vibration off ignores the marks") {
    const std::vector<Action> actions{{0, 20}, {1000, 30}, {2000, 20}};
    MapOptions o = SpatialOptions(WithMarks(Box(40, 40, 40, 40), Path({{0, 0, 0}, {1000, 60, 60}, {2000, 0, 0}})));
    o.contact_vibration = false;
    o.min_vibration = 0;
    MapOptions plain = SpatialOptions(ScriptMetadata{});
    plain.contact_vibration = false;
    plain.min_vibration = 0;
    for (int64_t at : {0, 500, 1000, 1500})
        CHECK(VibAt(actions, o, at) == doctest::Approx(VibAt(actions, plain, at)));
}

TEST_CASE("a .samn with marks and a tip path plays its spatial contact (MapOptionsFromScript)") {
    Document d;
    d.version = SamnCurrentVersion;
    d.kind = SamnKind;
    d.general = {{0, 20}, {1000, 30}, {2000, 20}};
    d.duration_ms = 2000;
    d.profile = "standard";
    DeviceRecipe r;
    r.sync = "independent";
    r.smoothing = 0.3;
    ApplyContactVibration(r, 0, "");
    d.recipe = r;
    ContactMarks cm;
    cm.primary = Box(40, 40, 40, 40, "nipples");
    d.contact_marks = cm;
    d.trajectory = Path({{0, 0, 0}, {1000, 60, 60}, {2000, 0, 0}});
    Document back = ParseDocument(MarshalDocument(d));
    const MapOptions o = MapOptionsFromScript(back.ToFunscript());
    REQUIRE(o.spatial_contact);
    CHECK(o.contact_vibration);
    CHECK(o.spatial_contact(1000) >= 0.99);
    CHECK(o.spatial_contact(0) == 0);
}

TEST_CASE("contact marks to store: nothing, stroke feel only, Tf/Tj drive, extras follow") {
    CHECK_FALSE(BuildContactMarks({}));

    ContactMarkInput in;
    in.profile = "standard";
    in.tip_class = "glans";
    in.tip = Box(1, 2, 50, 60);
    in.primary = Box(10, 20, 30, 40, "nipples");
    in.extras = {Box(50, 60, 20, 20, "nipples", true), Box(0, 0, 0, 5)};
    auto cm = BuildContactMarks(in);
    REQUIRE(cm);
    CHECK(cm->tip_class == "glans");
    REQUIRE(cm->tip);
    CHECK(cm->tip->w == 50);
    CHECK(cm->tip->cls == "glans");
    REQUIRE(cm->primary);
    CHECK(cm->primary->cls == "nipples");
    CHECK(cm->primary->w == 30);
    REQUIRE(cm->extras.size() == 1);  // the empty box is dropped
    CHECK(cm->extras[0].cls == "nipples");
    CHECK(cm->extras[0].fixed);
    CHECK_FALSE(cm->drive_stroke);

    ContactMarkInput tj;
    tj.profile = "tj";
    tj.primary = Box(1, 2, 3, 4);
    auto drive = BuildContactMarks(tj);
    REQUIRE(drive);
    CHECK(drive->drive_stroke);

    ContactMarkInput follow;
    follow.extras = {Box(50, 60, 20, 20, "nipples", false)};
    auto f = BuildContactMarks(follow);
    REQUIRE(f);
    CHECK_FALSE(f->extras[0].fixed);

    ContactMarkInput class_only;
    class_only.tip_class = "glans";
    CHECK(BuildContactMarks(class_only));
}

TEST_CASE("contact vibration in a recipe: only what differs from the defaults is written") {
    DeviceRecipe r;
    ApplyContactVibration(r, 0, "");
    CHECK(r.contact_vibration);
    CHECK(r.contact_vibration_span == 0);
    CHECK(r.contact_vibration_curve.empty());
    DeviceRecipe s;
    ApplyContactVibration(s, DefaultContactVibrationSpan, ContactCurveLinear);
    CHECK(s.contact_vibration_span == 0);
    CHECK(s.contact_vibration_curve.empty());
    DeviceRecipe t;
    ApplyContactVibration(t, 0.5, "soft");
    CHECK(t.contact_vibration_span == doctest::Approx(EffectiveContactSpan(0.5)));
    CHECK(t.contact_vibration_curve == "soft");
}
