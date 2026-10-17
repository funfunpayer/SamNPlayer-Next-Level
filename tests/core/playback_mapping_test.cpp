#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "samn/playback_mapping.h"
#include "samn/samn_file.h"

using namespace samn;

// 0.7 sam/runtime_test.go, the precedence of cmd/gui-wails/app_playback.go
// StartPlayback, the contact path of sam/playback.go and the Effects presets
// of frontend/src/player_layout.js. The "0.7 run" numbers come from the 0.7
// code itself (StartPlayback on the mock device, the frames it hands to the
// player), printed with %.17g.

namespace {

const char* kTj = R"({"actions":[{"at":0,"pos":20},{"at":200,"pos":40},{"at":400,"pos":70},{"at":600,"pos":90},
  {"at":800,"pos":55},{"at":1000,"pos":20},{"at":1200,"pos":85},{"at":1400,"pos":30},{"at":1600,"pos":20},
  {"at":1800,"pos":60},{"at":2000,"pos":95},{"at":2200,"pos":10},{"at":2400,"pos":50}],
  "metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,
  "max_speed":0.5,"smoothing":0.22,"contact_vibration":true,"contact_vibration_span":0.75},
  "tracking_gaps":[{"start_ms":1500,"end_ms":1700}]}})";

std::vector<Action> refActions() {
    const int pos[] = {20, 40, 70, 90, 55, 20, 85, 30, 20, 60, 95, 10, 50};
    std::vector<Action> out;
    for (int i = 0; i < 13; ++i) out.push_back({int64_t(i) * 200, pos[i]});
    return out;
}

// A .samn as the generator writes it: tj recipe with contact vibration and
// the default strength presets, `active` selected.
Document tjSamn(const std::string& active) {
    Document d;
    d.version = 1;
    d.kind = SamnKind;
    d.profile = "tj";
    d.recipe.sync = "suction_position";
    d.recipe.min_suction = 0.2;
    d.recipe.tick_ms = 50;
    d.recipe.max_speed = 0.5;
    d.recipe.smoothing = 0.22;
    d.recipe.contact_vibration = true;
    d.recipe.contact_vibration_span = 0.75;
    d.recipe.contact_vibration_curve = "linear";
    d.general = refActions();
    d.strength_presets = DefaultStrengthPresets();
    d.active_strength = active;
    d.Normalize();
    return d;
}

// Axes mode (explicit vibration / suction curves), preset "strong".
Document axesSamn() {
    Document d;
    d.version = 1;
    d.kind = SamnKind;
    d.profile = "standard";
    d.general = refActions();
    for (int i = 0; i <= 12; ++i) {
        d.vibration.push_back({int64_t(i) * 200, (i * 37) % 101});
        d.suction.push_back({int64_t(i) * 200, 100 - (i * 23) % 101});
    }
    d.playback_source = PlaybackAxes;
    d.recipe.sync = "independent";
    d.recipe.playback_source = PlaybackAxes;
    d.recipe.contact_vibration = true;
    d.recipe.contact_vibration_span = 0.7;
    d.strength_presets = DefaultStrengthPresets();
    d.active_strength = "strong";
    d.Normalize();
    return d;
}

std::vector<Frame> play(Document d, const PlaybackChoices& c) {
    const MapOptions base = MapOptionsFromScript(d.ToFunscript());
    return PlaybackFrames(d.general, PlanPlayback(base, d.profile, c, d.ActivePreset()));
}

std::vector<Frame> playFunscript(const char* json, const PlaybackChoices& c) {
    const FunscriptView v = ParseFunscript(json);
    return PlaybackFrames(v.PlaybackActions(), PlanPlayback(MapOptionsFromScript(v), v.metadata.profile, c, nullptr));
}

const Frame& at(const std::vector<Frame>& f, int64_t ms) {
    for (const auto& x : f)
        if (x.at == ms) return x;
    throw std::out_of_range("no frame");
}

double sumVib(const std::vector<Frame>& f) {
    return std::accumulate(f.begin(), f.end(), 0.0, [](double s, const Frame& x) { return s + x.vibration; });
}
double sumSuc(const std::vector<Frame>& f) {
    return std::accumulate(f.begin(), f.end(), 0.0, [](double s, const Frame& x) { return s + x.suction; });
}

PlaybackChoices choices(const std::string& sync, int64_t tick, double maxSpeed, double smoothing, double intensity) {
    PlaybackChoices c;
    c.sync_mode = sync;
    c.tick_ms = tick;
    c.max_speed = maxSpeed;
    c.smoothing = smoothing;
    c.contact_intensity = intensity;
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// Runtime adjust (0.7 sam/runtime_test.go)
// ---------------------------------------------------------------------------

TEST_CASE("RuntimeAdjust: the defaults change nothing (0.7 TestRuntimeAdjustIdentity)") {
    const std::vector<Frame> frames{{0, 0.5, 0.4}};
    const auto out = AdjustDeviceFrames(frames, RuntimeAdjust{});
    CHECK(out[0].vibration == 0.5);
    CHECK(out[0].suction == 0.4);
    CHECK(IsIdentity(RuntimeAdjust{}));
    CHECK(AdjustDeviceFrames({}, RuntimeAdjust{}).empty());
}

TEST_CASE("RuntimeAdjust: scale and mute (0.7 TestRuntimeAdjustScaleAndMute)") {
    const std::vector<Frame> frames{{0, 0.8, 0.5}, {50, 0.8, 0.5}};
    RuntimeAdjust half;
    half.intensity_scale = 0.5;
    CHECK(AdjustDeviceFrames(frames, half)[0].vibration == doctest::Approx(0.4).epsilon(1e-12));
    RuntimeAdjust mute;
    mute.mute_contact = true;
    const auto muted = AdjustDeviceFrames(frames, mute);
    CHECK(muted[0].vibration == 0);
    CHECK(muted[0].suction == 0.5);  // mute must not touch suction
}

TEST_CASE("RuntimeAdjust: boost clamps to 1 (0.7 TestRuntimeAdjustBoostClamp)") {
    RuntimeAdjust a;
    a.intensity_boost = 0.5;
    CHECK(AdjustDeviceFrames({{0, 0.9, 1}}, a)[0].vibration == 1);
}

TEST_CASE("RuntimeAdjust: ranges, extra smoothing and zero as a value (PS-11)") {
    const std::vector<Frame> frames{{0, 0.4, 0.2}, {50, 1, 1}, {100, 0, 0}};
    RuntimeAdjust big;
    big.intensity_scale = 5;  // clamped to 2
    big.suction_scale = 3;    // clamped to 2
    const auto b = AdjustDeviceFrames(frames, big);
    CHECK(b[0].vibration == doctest::Approx(0.8));
    CHECK(b[0].suction == doctest::Approx(0.4));
    CHECK(b[1].vibration == 1);  // clamped to 0-1 after the scale
    // Extra smoothing: EMA on both channels, from the second frame on;
    // clamped to 0.95.
    RuntimeAdjust sm;
    sm.extra_smooth = 0.5;
    const auto s = AdjustDeviceFrames(frames, sm);
    CHECK(s[0].vibration == 0.4);
    CHECK(s[1].vibration == doctest::Approx(0.7));
    CHECK(s[1].suction == doctest::Approx(0.6));
    CHECK(s[2].vibration == doctest::Approx(0.35));
    sm.extra_smooth = 2;
    CHECK(Normalized(sm).extra_smooth == 0.95);
    // A scale of 0 is silence, not "unset" (0.7 turned it into 1).
    RuntimeAdjust zero;
    zero.intensity_scale = 0;
    CHECK(AdjustDeviceFrames(frames, zero)[1].vibration == 0);
    zero.intensity_scale = std::numeric_limits<double>::quiet_NaN();  // not finite: the default
    CHECK(Normalized(zero).intensity_scale == 1);
}

// ---------------------------------------------------------------------------
// Strength presets (0.7 samn DefaultStrengthPresets)
// ---------------------------------------------------------------------------

TEST_CASE("Strength presets: soft, normal, strong with the 0.7 values") {
    const auto p = DefaultStrengthPresets();
    REQUIRE(p.size() == 3);
    CHECK(p[0].name == "soft");
    CHECK(p[0].vibration_scale == 0.7);
    CHECK(p[0].suction_scale == 0.85);
    CHECK(p[0].contact_span == 0.85);
    CHECK(p[0].contact_curve == "soft");
    CHECK(p[1].name == "normal");
    CHECK(p[1].vibration_scale == 1.0);
    CHECK(p[1].suction_scale == 1.0);
    CHECK(p[1].contact_span == 0);
    CHECK(p[1].contact_curve.empty());
    CHECK(p[2].name == "strong");
    CHECK(p[2].vibration_scale == 1.3);
    CHECK(p[2].suction_scale == 1.1);
    CHECK(p[2].contact_span == 0.55);
    CHECK(p[2].contact_curve == "peak");
}

// ---------------------------------------------------------------------------
// Precedence (StartPlayback)
// ---------------------------------------------------------------------------

TEST_CASE("PlanPlayback: tick and max speed replace the recipe only when > 0") {
    MapOptions base = RecipeFor("tj");  // tick 50, max speed 0.5
    base.tick_ms = 40;
    auto p = PlanPlayback(base, "tj", choices("", 100, 1.2, 0.3, 1), nullptr);
    CHECK(p.map.tick_ms == 100);
    CHECK(p.map.max_speed == 1.2);
    p = PlanPlayback(base, "tj", choices("", 0, 0, 0.3, 1), nullptr);
    CHECK(p.map.tick_ms == 40);
    CHECK(p.map.max_speed == 0.5);
}

TEST_CASE("PlanPlayback: smoothing, 0 included, for plain stroke scripts; tf/tj and axes keep theirs at 0") {
    MapOptions plain = DefaultMapOptions();
    plain.smoothing = 0.5;
    CHECK(PlanPlayback(plain, "", choices("", 50, 0.6, 0, 1), nullptr).map.smoothing == 0);
    CHECK(PlanPlayback(plain, "standard", choices("", 50, 0.6, 0.6, 1), nullptr).map.smoothing == 0.6);
    const MapOptions tj = RecipeFor("tj");  // 0.22
    CHECK(PlanPlayback(tj, "tj", choices("", 50, 0.6, 0, 1), nullptr).map.smoothing == 0.22);
    CHECK(PlanPlayback(tj, "tj", choices("", 50, 0.6, 0.4, 1), nullptr).map.smoothing == 0.4);
    MapOptions axes = DefaultMapOptions();
    axes.use_explicit_axes = true;
    axes.smoothing = 0.1;
    CHECK(PlanPlayback(axes, "standard", choices("", 50, 0.6, 0, 1), nullptr).map.smoothing == 0.1);
    CHECK(PlanPlayback(axes, "standard", choices("", 50, 0.6, 0.7, 1), nullptr).map.smoothing == 0.7);
}

TEST_CASE("PlanPlayback: the sync mode applies to plain scripts, to tf/tj only when not independent, never to axes") {
    MapOptions plain = DefaultMapOptions();
    plain.sync = SyncMode::Synchronized;  // a recipe of its own
    CHECK(PlanPlayback(plain, "standard", choices("independent", 50, 0.6, 0.3, 1), nullptr).map.sync ==
          SyncMode::Independent);
    CHECK(PlanPlayback(plain, "", choices("alternating", 50, 0.6, 0.3, 1), nullptr).map.sync == SyncMode::Alternating);
    const MapOptions tj = RecipeFor("tj");
    CHECK(PlanPlayback(tj, "tj", choices("independent", 50, 0.6, 0.3, 1), nullptr).map.sync == SyncMode::SuctionPosition);
    CHECK(PlanPlayback(tj, "tj", choices("", 50, 0.6, 0.3, 1), nullptr).map.sync == SyncMode::SuctionPosition);
    CHECK(PlanPlayback(tj, "tf", choices("synchronized", 50, 0.6, 0.3, 1), nullptr).map.sync == SyncMode::Synchronized);
    MapOptions axes = DefaultMapOptions();
    axes.use_explicit_axes = true;
    CHECK(PlanPlayback(axes, "standard", choices("vibration_only", 50, 0.6, 0.3, 1), nullptr).map.sync ==
          SyncMode::Independent);
    CHECK_THROWS_AS(PlanPlayback(plain, "", choices("sideways", 50, 0.6, 0.3, 1), nullptr), std::invalid_argument);
}

TEST_CASE("PlanPlayback: live contact knobs only for tf/tj or contact scripts, the contact path only in recipe mode") {
    PlaybackChoices c = choices("independent", 50, 0.6, 0.3, 1);
    c.contact_span = 0.5;
    c.contact_curve = std::string("peak");
    MapOptions plain = DefaultMapOptions();  // no contact vibration
    auto p = PlanPlayback(plain, "standard", c, nullptr);
    CHECK(p.map.contact_vibration_span == 0);
    CHECK(p.map.contact_vibration_curve.empty());
    CHECK(!p.contact_on);
    MapOptions stroke = DefaultMapOptions();
    stroke.contact_vibration = true;
    stroke.contact_vibration_span = 0.7;
    p = PlanPlayback(stroke, "standard", c, nullptr);
    CHECK(p.map.contact_vibration_span == 0.5);
    CHECK(p.map.contact_vibration_curve == "peak");
    CHECK(p.contact_on);
    MapOptions tj = RecipeFor("tj");  // distance profile, contact off in the recipe
    p = PlanPlayback(tj, "tj", c, nullptr);
    CHECK(p.map.contact_vibration_span == 0.5);
    CHECK(!p.contact_on);
    MapOptions axes = stroke;
    axes.use_explicit_axes = true;
    p = PlanPlayback(axes, "standard", c, nullptr);
    CHECK(p.map.contact_vibration_span == 0.5);
    CHECK(!p.contact_on);
    // Contact off: no contact path and the vibration is muted.
    c.contact_off = true;
    p = PlanPlayback(stroke, "standard", c, nullptr);
    CHECK(!p.map.contact_vibration);
    CHECK(!p.contact_on);
    CHECK(p.adjust.mute_contact);
}

TEST_CASE("PlanPlayback: the strength preset scales vibration and suction and may set span and curve") {
    const auto presets = DefaultStrengthPresets();
    MapOptions tj = RecipeFor("tj");
    tj.contact_vibration = true;
    tj.contact_vibration_span = 0.75;
    PlaybackChoices c = choices("independent", 50, 0.6, 0.3, 0.5);
    c.contact_span = 0.6;
    auto p = PlanPlayback(tj, "tj", c, &presets[0]);  // soft
    CHECK(p.adjust.intensity_scale == doctest::Approx(0.35));
    CHECK(p.suction_scale == 0.85);
    CHECK(p.map.contact_vibration_span == 0.85);  // the preset wins over the live knob
    CHECK(p.map.contact_vibration_curve == "soft");
    CHECK(!p.adjust.mute_contact);
    // Intensity 0: the preset scale stays, the vibration is muted.
    c.contact_intensity = 0;
    p = PlanPlayback(tj, "tj", c, &presets[2]);  // strong
    CHECK(p.adjust.intensity_scale == doctest::Approx(1.3));
    CHECK(p.suction_scale == 1.1);
    CHECK(p.adjust.mute_contact);
    CHECK(p.map.contact_vibration_curve == "peak");
    // normal: span / curve stay the live ones.
    c.contact_intensity = 1;
    p = PlanPlayback(tj, "tj", c, &presets[1]);
    CHECK(p.map.contact_vibration_span == 0.6);
    // Axes mode: no span / curve from the preset, the scales still apply.
    MapOptions axes = tj;
    axes.use_explicit_axes = true;
    p = PlanPlayback(axes, "tj", choices("independent", 50, 0.6, 0.3, 1), &presets[2]);
    CHECK(p.map.contact_vibration_span == 0.75);
    CHECK(p.adjust.intensity_scale == doctest::Approx(1.3));
}

TEST_CASE("ChoicesForScript: without contact in the recipe the contact choices are not sent (0.7 playback.js)") {
    PlaybackChoices c = choices("independent", 50, 0.6, 0.3, 0.2);
    c.contact_off = true;
    c.contact_span = 0.5;
    c.contact_curve = std::string("peak");
    const auto off = ChoicesForScript(c, false);
    CHECK(off.contact_intensity == 1);
    CHECK(!off.contact_off);
    CHECK(!off.contact_span);
    CHECK(!off.contact_curve);
    const auto on = ChoicesForScript(c, true);
    CHECK(on.contact_intensity == 0.2);
    CHECK(on.contact_off);
    CHECK(*on.contact_span == 0.5);
}

// ---------------------------------------------------------------------------
// The whole Play path against the 0.7 run (48 cases compared frame by frame,
// largest difference 1.1e-16; these are a sample).
// ---------------------------------------------------------------------------

TEST_CASE("Play frames match the 0.7 run: plain script, synchronized, tick 100, max speed 1.2, smoothing 0") {
    const auto f = playFunscript(R"({"actions":[{"at":0,"pos":20},{"at":200,"pos":40},{"at":400,"pos":70},
        {"at":600,"pos":90},{"at":800,"pos":55},{"at":1000,"pos":20},{"at":1200,"pos":85},{"at":1400,"pos":30},
        {"at":1600,"pos":20},{"at":1800,"pos":60},{"at":2000,"pos":95},{"at":2200,"pos":10},{"at":2400,"pos":50}]})",
                                 choices("synchronized", 100, 1.2, 0, 0.5));
    REQUIRE(f.size() == 25);
    CHECK(sumVib(f) == doctest::Approx(3.6104166666666671).epsilon(1e-12));
    CHECK(sumSuc(f) == doctest::Approx(4.083333333333333).epsilon(1e-12));
    CHECK(at(f, 600).vibration == doctest::Approx(0.13697916666666665).epsilon(1e-12));
    CHECK(at(f, 600).suction == doctest::Approx(0.14583333333333334).epsilon(1e-12));
}

TEST_CASE("Play frames match the 0.7 run: tj contact path with the default settings") {
    const auto f = playFunscript(kTj, choices("independent", 50, 0.6, 0.3, 1));
    REQUIRE(f.size() == 49);
    CHECK(sumVib(f) == doctest::Approx(2.9566005643382347).epsilon(1e-12));
    CHECK(sumSuc(f) == doctest::Approx(24.639334611263454).epsilon(1e-12));
    CHECK(at(f, 600).vibration == doctest::Approx(0.5006556985294117).epsilon(1e-12));
    CHECK(at(f, 1200).suction == doctest::Approx(0.7812249686371991).epsilon(1e-12));
    CHECK(at(f, 1600).vibration == 0);  // tracking gap
}

TEST_CASE("Play frames match the 0.7 run: tj with tick/max speed/smoothing 0, intensity 2, impulse curve") {
    PlaybackChoices c = choices("", 0, 0, 0, 2);
    c.contact_curve = std::string("impulse");
    const auto f = playFunscript(kTj, c);
    REQUIRE(f.size() == 49);
    CHECK(sumVib(f) == doctest::Approx(5.9140351456929423).epsilon(1e-12));
    CHECK(sumSuc(f) == doctest::Approx(24.673281802214596).epsilon(1e-12));
    CHECK(at(f, 600).vibration == 1);
}

TEST_CASE("Play frames match the 0.7 run: .samn strength presets soft and strong") {
    const auto soft = play(tjSamn("soft"), choices("independent", 50, 0.6, 0.3, 1));
    REQUIRE(soft.size() == 49);
    CHECK(sumVib(soft) == doctest::Approx(0.44996967512495184).epsilon(1e-12));
    CHECK(sumSuc(soft) == doctest::Approx(20.94343441957394).epsilon(1e-12));
    CHECK(at(soft, 600).vibration == doctest::Approx(0.10897601883890808).epsilon(1e-12));
    PlaybackChoices k = choices("independent", 50, 0.6, 0.6, 1.5);
    k.contact_span = 0.5;
    k.contact_curve = std::string("peak");
    const auto strong = play(tjSamn("strong"), k);
    CHECK(sumVib(strong) == doctest::Approx(13.897703496478131).epsilon(1e-12));
    CHECK(sumSuc(strong) == doctest::Approx(26.889706676656289).epsilon(1e-12));
    CHECK(at(strong, 1200).vibration == doctest::Approx(0.6209835009655776).epsilon(1e-12));
}

TEST_CASE("Play frames match the 0.7 run: .samn axes mode with preset strong") {
    const auto f = play(axesSamn(), choices("independent", 50, 0.6, 0.3, 1));
    REQUIRE(f.size() == 49);
    CHECK(sumVib(f) == doctest::Approx(27.972611443181233).epsilon(1e-12));
    CHECK(sumSuc(f) == doctest::Approx(29.694514822810394).epsilon(1e-12));
    CHECK(at(f, 300).vibration == doctest::Approx(0.67000185525).epsilon(1e-12));
    CHECK(at(f, 1600).vibration == 1);
}

TEST_CASE("ContactDeviceFrames: only for contact vibration with suction from position") {
    MapOptions o = RecipeFor("tj");
    CHECK(!ContactDeviceFrames(refActions(), o));  // contact off
    o.contact_vibration = true;
    CHECK(ContactDeviceFrames(refActions(), o));
    o.sync = SyncMode::Synchronized;
    CHECK(!ContactDeviceFrames(refActions(), o));
    o.sync = SyncMode::SuctionPosition;
    CHECK(!ContactDeviceFrames({{0, 50}}, o));
}

// 0.7 sam/playback_test.go and runtime_test.go (Densify): the contact path.
namespace {

const char* kContactScript = R"({"actions":[{"at":0,"pos":20},{"at":500,"pos":20},{"at":600,"pos":90},
  {"at":700,"pos":90},{"at":900,"pos":90},{"at":1000,"pos":20},{"at":1500,"pos":20}],
  "metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,"tick_ms":50,
  "max_speed":0.5,"smoothing":0.22,"contact_vibration":true}}})";

double maxVib(const std::vector<Frame>& f, int64_t from, int64_t to) {
    double m = 0;
    for (const auto& x : f)
        if (x.at >= from && x.at <= to) m = std::max(m, x.vibration);
    return m;
}

}  // namespace

TEST_CASE("Contact path: intensity from the position, gaps hold 0 (0.7 TestPlaybackFramesUsesSAMIntensity)") {
    const FunscriptView fs = ParseFunscript(kContactScript);
    MapOptions o = RecipeFor("tj");
    o.contact_vibration = true;
    o.contact_vibration_envelope = -1;
    o.smoothing = 0;
    o.tracking_gaps = {{650, 850}};
    const auto f = ContactDeviceFrames(fs.PlaybackActions(), o);
    REQUIRE(f);
    CHECK(maxVib(*f, 400, 500) <= 0.001);  // far
    CHECK(maxVib(*f, 600, 640) >= 0.5);    // contact
    CHECK(maxVib(*f, 700, 800) <= 0.001);  // gap
}

TEST_CASE("Contact path: without contact vibration Play maps with the mapper (0.7 TestPlaybackFramesFallsBackWithoutContact)") {
    const FunscriptView fs = ParseFunscript(R"({"actions":[{"at":0,"pos":20},{"at":500,"pos":90},{"at":1000,"pos":20}],
        "metadata":{"profile":"tj"}})");
    PlaybackPlan plan;
    plan.map = RecipeFor("tj");
    plan.map.smoothing = 0;
    CHECK(!ContactDeviceFrames(fs.PlaybackActions(), plan.map));
    plan.contact_on = true;  // even then: the mapper
    const auto f = PlaybackFrames(fs.PlaybackActions(), plan);
    REQUIRE(!f.empty());
    for (const auto& x : f) {
        CHECK(x.vibration <= 0.001);
        CHECK(x.suction >= 0.1);  // suction follows the position
    }
}

TEST_CASE("Contact path: gaps from the options mute like the mapper (0.7 TestPlaybackFramesHonorsOptsOnlyTrackingGaps)") {
    const FunscriptView fs = ParseFunscript(kContactScript);
    MapOptions o = RecipeFor("tj");
    o.contact_vibration = true;
    o.contact_vibration_envelope = -1;
    o.smoothing = 0;
    o.min_vibration = 0;
    o.tracking_gaps = {{650, 850}};
    const auto classic = ToIntensityCurve(fs.PlaybackActions(), o);
    REQUIRE(classic);
    CHECK(maxVib(*classic, 700, 800) <= 0.001);
    const auto f = ContactDeviceFrames(fs.PlaybackActions(), o);
    REQUIRE(f);
    CHECK(maxVib(*f, 700, 800) <= 0.001);
    CHECK(maxVib(*f, 600, 640) >= 0.5);
}

TEST_CASE("Contact path: no vibration below the contact window (0.7 TestToDeviceFramesNoVibBelowContactThreshold)") {
    const FunscriptView fs = ParseFunscript(R"({"actions":[{"at":0,"pos":20},{"at":500,"pos":20},{"at":1000,"pos":90},
        {"at":1500,"pos":90}],"metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,
        "tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true,"contact_vibration_span":0.5}}})");
    MapOptions o = RecipeFor("tj");
    o.contact_vibration = true;
    o.contact_vibration_span = 0.5;  // window from 20 + 0.5 x 70 = 55
    o.contact_vibration_envelope = -1;
    o.smoothing = 0;
    o.min_vibration = 0;
    const auto f = ContactDeviceFrames(fs.PlaybackActions(), o);
    REQUIRE(f);
    CHECK(maxVib(*f, 650, 720) <= 0.001);  // pos about 48 at 700 ms
    CHECK(maxVib(*f, 1000, 1200) >= 0.5);
}

TEST_CASE("Contact path follows the mapper closely, gaps never leak (0.7 TestDensifyMatchesClassicLinear, TestDensifySourceAndFrameCount)") {
    const FunscriptView fs = ParseFunscript(R"({"actions":[{"at":0,"pos":20},{"at":200,"pos":40},{"at":400,"pos":70},
        {"at":600,"pos":90},{"at":800,"pos":55},{"at":1000,"pos":20},{"at":1200,"pos":85},{"at":1400,"pos":30},
        {"at":1600,"pos":20}],"metadata":{"profile":"tj","device_recipe":{"sync":"suction_position","min_suction":0.2,
        "tick_ms":50,"max_speed":0.5,"smoothing":0.22,"contact_vibration":true,"contact_vibration_span":0.5},
        "tracking_gaps":[{"start_ms":750,"end_ms":950}]}})");
    MapOptions o = RecipeFor("tj");
    o.contact_vibration = true;
    o.contact_vibration_span = 0.5;
    o.contact_vibration_curve = "linear";
    o.contact_vibration_envelope = -1;
    o.smoothing = 0;
    o.min_vibration = 0;
    o.tracking_gaps = fs.metadata.tracking_gaps;
    const auto classic = ToIntensityCurve(fs.PlaybackActions(), o);
    const auto f = ContactDeviceFrames(fs.PlaybackActions(), o);
    REQUIRE(classic);
    REQUIRE(f);
    REQUIRE(classic->size() == f->size());
    CHECK(f->size() >= 20);  // one frame per tick
    double maxDiff = 0;
    for (size_t i = 0; i < f->size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs((*classic)[i].vibration - (*f)[i].vibration));
        if ((*f)[i].at >= 750 && (*f)[i].at <= 950) CHECK((*f)[i].vibration <= 0.001);
    }
    CHECK(maxDiff <= 0.02);
}

// ---------------------------------------------------------------------------
// Effects presets (0.7 app_effect_presets_test.go and player_layout.js)
// ---------------------------------------------------------------------------

TEST_CASE("Effects presets: the 0.7 text reads and writes back (0.7 TestEffectPresetsRoundTrip)") {
    const std::string text =
        R"({"Night":{"intensity":40,"smoothing":0.8,"maxSpeed":0.4,"tickMs":80,"softStartMs":1500,"reducePeaks":true}})";
    const auto p = ParseEffectPresets(text);
    REQUIRE(p.count("Night") == 1);
    const auto& n = p.at("Night");
    CHECK(*n.intensity == 40);
    CHECK(*n.smoothing == 0.8);
    CHECK(*n.max_speed == 0.4);
    CHECK(*n.tick_ms == 80);
    CHECK(*n.soft_start_ms == 1500);
    CHECK(*n.reduce_peaks);
    CHECK(ParseEffectPresets(SerializeEffectPresets(p)).at("Night").soft_start_ms == 1500.0);
    CHECK(SerializeEffectPresets({}) == "{}");
}

TEST_CASE("Effects presets: broken text gives none, broken entries are skipped, missing fields stay unset") {
    CHECK(ParseEffectPresets("").empty());
    CHECK(ParseEffectPresets("not json").empty());
    CHECK(ParseEffectPresets("[1,2]").empty());
    const auto p = ParseEffectPresets(R"({"a":5,"b":{"intensity":"x","tickMs":100},"c":{}})");
    CHECK(p.count("a") == 0);
    REQUIRE(p.count("b") == 1);
    CHECK(!p.at("b").intensity);
    CHECK(*p.at("b").tick_ms == 100);
    CHECK(!p.at("c").reduce_peaks);
    // Unset fields are left out when written.
    CHECK(SerializeEffectPresets(p) == R"({"b":{"tickMs":100},"c":{}})");
}

TEST_CASE("Effects presets: Default has the 0.7 FX_DEFAULTS; names are trimmed, 40 characters, not __default") {
    const auto d = DefaultEffectPreset();
    CHECK(*d.intensity == 100);
    CHECK(*d.smoothing == 0.3);
    CHECK(*d.max_speed == 0.6);
    CHECK(*d.tick_ms == 50);
    CHECK(*d.soft_start_ms == 500);
    CHECK(!*d.reduce_peaks);
    CHECK(CleanPresetName("  Night  ") == "Night");
    CHECK(CleanPresetName("   ").empty());
    CHECK(CleanPresetName("__default").empty());
    CHECK(CleanPresetName(std::string(50, 'x')) == std::string(40, 'x'));
    CHECK(CleanPresetName("\xC3\xBC" + std::string(45, 'a')).size() == 2 + 39);  // code points, not bytes
}
