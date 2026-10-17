// Port of the 0.7 funscript recipe tests: recipe.go (tj/tf profile, contact
// vibration), feel_scene_test.go (scene feel, follow-suction, balance),
// axes_test.go and inverted_playback_test.go. The .samn side of the same
// features (ApplyContactRecipe, ApplyFeelRecipe) is in samn_file_test.cpp.
#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "samn/funscript.h"
#include "samn/mapper.h"
#include "samn/samn_file.h"

using samn::Action;
using samn::Frame;
using samn::MapOptions;

namespace {

std::vector<Frame> Curve(const std::vector<Action>& a, const MapOptions& o) {
    auto f = samn::ToIntensityCurve(a, o);
    REQUIRE(f.has_value());
    return *f;
}

// Go: strokeActions (feel_scene_test.go)
std::vector<Action> StrokeActions() {
    std::vector<Action> a;
    for (int i = 0; i <= 40; ++i) a.push_back({int64_t{i} * 400, i % 2 == 1 ? 90 : 10});
    return a;
}

// Go: meanFrames. Returns {vibration, suction}.
std::pair<double, double> MeanFrames(const std::vector<Frame>& fr) {
    double vib = 0, suc = 0;
    for (const auto& f : fr) {
        vib += f.vibration;
        suc += f.suction;
    }
    const double n = static_cast<double>(fr.size());
    return {vib / n, suc / n};
}

MapOptions TjContactOptions() {
    MapOptions o = samn::RecipeFor("tj");
    o.contact_vibration = true;
    o.contact_vibration_envelope = -1;  // exact assertions without the envelope
    o.smoothing = 0;
    return o;
}

}  // namespace

// ---------------------------------------------------------------------------
// recipe_test.go
// ---------------------------------------------------------------------------

TEST_CASE("recipe: profile names and default sync") {  // TestNormalizeProfile, TestDefaultSyncForProfile
    CHECK(samn::NormalizeProfile("TF") == samn::ProfileTj);
    CHECK(samn::NormalizeProfile("tj") == samn::ProfileTj);
    CHECK(samn::IsDistanceProfile("tf"));
    CHECK(samn::IsDistanceProfile("TJ"));
    CHECK_FALSE(samn::IsDistanceProfile("standard"));
    // IsStrokeProfile / AllowsContactSettings have no C++ counterpart yet.
    CHECK(samn::DefaultSyncForProfile("tj") == samn::SyncModeName(samn::SyncMode::SuctionPosition));
    CHECK(samn::DefaultSyncForProfile("standard") == samn::SyncModeName(samn::SyncMode::Independent));
    CHECK(samn::DefaultSyncForProfile("autotune") == samn::SyncModeName(samn::SyncMode::Independent));
}

TEST_CASE("recipe: tj is suction only, no vibration") {  // TestRecipeTJSuctionOnlyNoVibration
    MapOptions opts = samn::RecipeFor("tj");
    opts.smoothing = 0;
    auto frames = Curve({{0, 20}, {800, 90}, {1600, 20}}, opts);
    REQUIRE(!frames.empty());
    double max_suc = 0, max_vib = 0, min_suc = 1.0;
    for (const auto& f : frames) {
        max_vib = std::max(max_vib, f.vibration);
        max_suc = std::max(max_suc, f.suction);
        min_suc = std::min(min_suc, f.suction);
    }
    CHECK_MESSAGE(max_vib <= 0.001, "tf/tj must not vibrate, max ", max_vib);
    CHECK_MESSAGE(max_suc >= 0.7, "suction must follow the high position, max ", max_suc);
    // The floor comes from the 20-90 script clamp, not from a second LiftFloor.
    CHECK_MESSAGE(min_suc >= 0.15, "suction floor ", min_suc);
    CHECK_MESSAGE(min_suc <= 0.25, "suction floor ", min_suc);
    CHECK_MESSAGE(max_suc <= 0.95, "suction max should be about 0.90 (no double floor), ", max_suc);
}

TEST_CASE("recipe: tj suction has no double floor") {  // TestRecipeTJSuctionNoDoubleFloor
    MapOptions opts = samn::RecipeFor("tj");
    opts.smoothing = 0;
    auto frames = Curve({{0, 20}, {500, 20}, {1000, 90}, {1500, 90}}, opts);
    double at_low = 0, at_high = 0;
    bool saw_low = false, saw_high = false;
    for (const auto& f : frames) {
        if (f.at <= 400) {
            at_low = f.suction;
            saw_low = true;
        }
        if (f.at >= 1200 && f.at <= 1400) {
            at_high = f.suction;
            saw_high = true;
        }
    }
    REQUIRE(saw_low);
    REQUIRE(saw_high);
    CHECK_MESSAGE(at_low >= 0.18, "pos 20 should give about 0.20 suction, got ", at_low);
    CHECK_MESSAGE(at_low <= 0.22, "pos 20 should give about 0.20 suction, got ", at_low);
    CHECK_MESSAGE(at_high >= 0.88, "pos 90 should give about 0.90 suction, got ", at_high);
    CHECK_MESSAGE(at_high <= 0.92, "pos 90 should give about 0.90 suction, got ", at_high);
    CHECK(opts.min_suction == 0.20);
}

TEST_CASE("recipe: tj contact vibration is off by default") {  // TestRecipeTJContactVibrationOffByDefault
    MapOptions opts = samn::RecipeFor("tj");
    opts.smoothing = 0;
    auto frames = Curve({{0, 20}, {500, 90}, {1000, 90}, {1500, 20}}, opts);
    REQUIRE(!frames.empty());
    for (const auto& f : frames) CHECK_MESSAGE(f.vibration <= 0.001, "vibration ", f.vibration, " at ", f.at);
}

TEST_CASE("recipe: tj contact vibration tracks the position") {  // TestRecipeTJContactVibrationTracksPosition
    const std::vector<Action> script{{0, 20},    {500, 20}, {600, 90},  // contact near the observed maximum
                                     {900, 90},  {1000, 20}, {1500, 20}};
    auto frames = Curve(script, TjContactOptions());
    REQUIRE(!frames.empty());
    double far_vib = 0, contact_vib = 0, max_vib = 0;
    int contact_frames = 0;
    for (const auto& f : frames) {
        if (f.at < 400 || (f.at > 1100 && f.at < 1500)) far_vib = std::max(far_vib, f.vibration);
        if (f.at >= 700 && f.at <= 850) {
            ++contact_frames;
            contact_vib = std::max(contact_vib, f.vibration);
        }
        max_vib = std::max(max_vib, f.vibration);
    }
    CHECK_MESSAGE(far_vib <= 0.001, "far from contact vibration is 0, got ", far_vib);
    REQUIRE(contact_frames > 0);
    CHECK_MESSAGE(contact_vib >= 0.5, "sustained contact vibrates, got ", contact_vib);
    CHECK_MESSAGE(max_vib <= 1.0001, "vibration above 1: ", max_vib);
}

TEST_CASE("recipe: tj contact vibration, brief graze") {  // TestRecipeTJContactVibrationBriefGraze
    const std::vector<Action> sustained{{0, 20}, {500, 20}, {600, 90}, {900, 90}, {1000, 20}, {1500, 20}};
    const std::vector<Action> brief{{0, 20}, {500, 20}, {600, 90}, {650, 20}, {1500, 20}};
    auto count_and_peak = [](const std::vector<Action>& s) {
        int above_half = 0;
        double peak = 0;
        for (const auto& f : Curve(s, TjContactOptions())) {
            peak = std::max(peak, f.vibration);
            if (f.vibration > 0.5) ++above_half;
        }
        return std::make_pair(above_half, peak);
    };
    const auto [sustained_frames, sustained_peak] = count_and_peak(sustained);
    const auto [brief_frames, brief_peak] = count_and_peak(brief);
    CHECK_MESSAGE(brief_peak >= 0.9, "a brief graze reaches the same peak, got ", brief_peak);
    REQUIRE_MESSAGE(sustained_peak >= 0.9, "sustained contact should reach about 1.0, got ", sustained_peak);
    CHECK_MESSAGE(brief_frames != 0, "a brief graze must still vibrate");
    CHECK_MESSAGE(brief_frames < sustained_frames, brief_frames, " frames above 0.5 vs ", sustained_frames);
}

TEST_CASE("recipe: tj contact vibration needs a position span") {  // TestRecipeTJContactVibrationNeedsSpan
    auto frames = Curve({{0, 50}, {500, 52}, {1000, 50}}, TjContactOptions());
    REQUIRE(!frames.empty());
    for (const auto& f : frames) CHECK_MESSAGE(f.vibration <= 0.001, "vibration ", f.vibration, " at ", f.at);
}

TEST_CASE("recipe: tj contact vibration, a smaller span starts earlier") {  // TestRecipeTJContactVibrationSpanEarlier
    const std::vector<Action> script{{0, 20}, {500, 55}, {1000, 55}, {1500, 90}};
    auto at_mid = [&](double span) {
        MapOptions opts = TjContactOptions();
        opts.contact_vibration_span = span;
        double mx = 0;
        for (const auto& f : Curve(script, opts))
            if (f.at >= 500 && f.at <= 1000) mx = std::max(mx, f.vibration);
        return mx;
    };
    CHECK_MESSAGE(at_mid(0.75) <= 0.001, "default span 0.75 is still silent at pos 55");
    CHECK_MESSAGE(at_mid(0.45) >= 0.05, "span 0.45 vibrates at pos 55, got ", at_mid(0.45));
}

TEST_CASE("recipe: tj contact vibration curves") {  // TestRecipeTJContactVibrationCurves
    const std::vector<Action> script{{0, 20}, {500, 80}, {800, 80}, {1200, 90}, {1500, 20}};
    auto vib_at = [&](const char* curve) {
        MapOptions opts = TjContactOptions();
        opts.contact_vibration_curve = curve;
        double mx = 0;
        for (const auto& f : Curve(script, opts))
            if (f.at >= 550 && f.at <= 750) mx = std::max(mx, f.vibration);
        return mx;
    };
    const double linear = vib_at("linear"), soft = vib_at("soft"), peak = vib_at("peak");
    REQUIRE_MESSAGE(linear >= 0.1, "pos 80 should be in the contact window, linear=", linear);
    CHECK_MESSAGE(soft < linear, "soft=", soft, " linear=", linear);
    CHECK_MESSAGE(peak > linear, "peak=", peak, " linear=", linear);
    // Mid-window stays quiet under impulse; soft still has fill there.
    const double impulse = vib_at("impulse");
    CHECK_MESSAGE(impulse <= 0.05, "impulse=", impulse, " soft=", soft);
}

TEST_CASE("recipe: impulse curve peaks") {  // TestApplyContactCurveImpulsePeaks
    CHECK(samn::ApplyContactCurve(0.5, samn::ContactCurveImpulse) == 0);
    const double near = samn::ApplyContactCurve(0.9, samn::ContactCurveImpulse);
    const double top = samn::ApplyContactCurve(1.0, samn::ContactCurveImpulse);
    CHECK_MESSAGE(near > 0, "near=", near);
    CHECK_MESSAGE(near < top, "near=", near, " top=", top);
    CHECK_MESSAGE(top >= 0.999, "impulse at t=1 wants about 1, got ", top);
    CHECK(samn::NormalizeContactCurve("events") == samn::ContactCurveImpulse);
}

TEST_CASE("recipe: tj contact vibration is muted in a tracking gap") {  // TestRecipeTJContactVibrationMutedInTrackingGap
    MapOptions opts = TjContactOptions();
    opts.tracking_gaps = {{600, 900}};
    double in_gap = 0, outside = 0;
    for (const auto& f : Curve({{0, 20}, {500, 90}, {1000, 90}, {1500, 20}}, opts)) {
        if (f.at >= 650 && f.at <= 850) in_gap = std::max(in_gap, f.vibration);
        if (f.at >= 950 && f.at <= 1000) outside = std::max(outside, f.vibration);
    }
    CHECK_MESSAGE(in_gap <= 0.001, "vibration in the gap is 0, got ", in_gap);
    CHECK_MESSAGE(outside >= 0.5, "vibration outside the gap at pos 90 is high, got ", outside);
}

TEST_CASE("recipe: tracking gap wins over envelope and smoothing") {  // TestRecipeTJContactGapWinsOverEnvelopeAndSmoothing
    MapOptions opts = samn::RecipeFor("tj");
    opts.contact_vibration = true;
    opts.contact_vibration_envelope = 0.45;
    opts.smoothing = 0.3;
    opts.tracking_gaps = {{250, 700}};
    auto frames = Curve({{0, 90}, {200, 90}, {800, 90}, {1000, 20}}, opts);
    REQUIRE(!frames.empty());
    for (const auto& f : frames)
        if (f.at >= 350 && f.at <= 650) CHECK_MESSAGE(f.vibration <= 0.001, "at=", f.at, " vib=", f.vibration);
}

TEST_CASE("recipe: stroke profile contact vibration tracks the position") {  // TestRecipeStandardContactVibrationTracksPosition
    MapOptions opts = samn::DefaultMapOptions();
    opts.contact_vibration = true;
    opts.contact_vibration_envelope = -1;
    opts.smoothing = 0;
    opts.min_vibration = 0;
    auto frames = Curve({{0, 20}, {800, 20}, {900, 90}, {1200, 90}, {1300, 20}, {1800, 20}}, opts);
    double far_vib = 0, deep_vib = 0;
    for (const auto& f : frames) {
        if (f.at >= 200 && f.at <= 700) far_vib = std::max(far_vib, f.vibration);
        if (f.at >= 1000 && f.at <= 1150) deep_vib = std::max(deep_vib, f.vibration);
    }
    CHECK_MESSAGE(far_vib <= 0.05, "flat far window stays near 0, got ", far_vib);
    CHECK_MESSAGE(deep_vib >= 0.5, "flat deep window drives contact vibration, got ", deep_vib);
}

TEST_CASE("recipe: tf equals tj") {  // TestRecipeTFEqualsTJ
    const MapOptions a = samn::RecipeFor("tf"), b = samn::RecipeFor("tj");
    CHECK(a.sync == b.sync);
    CHECK(a.min_suction == b.min_suction);
    CHECK(a.sync == samn::SyncMode::SuctionPosition);
}

// ---------------------------------------------------------------------------
// feel_scene_test.go
// ---------------------------------------------------------------------------

TEST_CASE("feel: scene names and aliases") {  // TestNormalizeSceneFeelAliases
    const std::vector<std::pair<std::string, std::string>> cases{
        {"oral", samn::SceneFeelOral},         {"Blowjob", samn::SceneFeelOral},
        {"penetration", samn::SceneFeelIntercourse}, {"titjob", samn::SceneFeelBreast},
        {"handjob", samn::SceneFeelHand},      {"", samn::SceneFeelNone},
        {"dance", samn::SceneFeelNone},
    };
    for (const auto& [in, want] : cases) CHECK_MESSAGE(samn::NormalizeSceneFeel(in) == want, "input \"", in, "\"");
}

TEST_CASE("feel: scene feel off keeps the old mapping") {  // TestSceneFeelOffKeepsOldMapping
    const auto actions = StrokeActions();
    auto base = Curve(actions, samn::DefaultMapOptions());
    MapOptions o = samn::DefaultMapOptions();
    o.scene_feel = "";
    o.vibration_follows_suction = false;
    o.balance = 0;
    auto again = Curve(actions, o);
    REQUIRE(base.size() == again.size());
    for (size_t i = 0; i < base.size(); ++i) {
        CHECK_MESSAGE(base[i].at == again[i].at, "frame ", i);
        CHECK_MESSAGE(base[i].vibration == again[i].vibration, "frame ", i);
        CHECK_MESSAGE(base[i].suction == again[i].suction, "frame ", i);
    }
}

TEST_CASE("feel: suction shapes per scene") {  // TestSceneSuctionShapes
    // Oral keeps a seal even at the shallowest point.
    auto seal = samn::SceneSuction(samn::SceneFeelOral, 0, 0);
    REQUIRE(seal.has_value());
    CHECK_MESSAGE(*seal >= 0.29, "oral seal ", *seal);
    // Deeper pulls harder for every scene.
    for (const char* f : {samn::SceneFeelOral, samn::SceneFeelIntercourse, samn::SceneFeelBreast, samn::SceneFeelHand}) {
        auto lo = samn::SceneSuction(f, 0.1, 0.5);
        auto hi = samn::SceneSuction(f, 0.9, 0.5);
        REQUIRE(lo.has_value());
        REQUIRE(hi.has_value());
        CHECK_MESSAGE(*hi > *lo, f, ": deeper should pull harder (", *hi, " <= ", *lo, ")");
    }
    // Hand follows speed more than depth; breast follows depth more than speed.
    auto h_speed = samn::SceneSuction(samn::SceneFeelHand, 0.5, 1);
    auto h_depth = samn::SceneSuction(samn::SceneFeelHand, 1, 0.5);
    REQUIRE(h_speed.has_value());
    REQUIRE(h_depth.has_value());
    CHECK_MESSAGE(*h_speed > *h_depth, "hand: speed should weigh more (", *h_speed, " <= ", *h_depth, ")");
    auto b_speed = samn::SceneSuction(samn::SceneFeelBreast, 0.5, 1);
    auto b_depth = samn::SceneSuction(samn::SceneFeelBreast, 1, 0.5);
    REQUIRE(b_speed.has_value());
    REQUIRE(b_depth.has_value());
    CHECK_MESSAGE(*b_depth > *b_speed, "breast: depth should weigh more (", *b_depth, " <= ", *b_speed, ")");
    CHECK_FALSE(samn::SceneSuction(samn::SceneFeelNone, 0.5, 0.5).has_value());
}

TEST_CASE("feel: oral scene raises suction") {  // TestOralSceneRaisesSuction
    const auto actions = StrokeActions();
    const double old_suc = MeanFrames(Curve(actions, samn::DefaultMapOptions())).second;
    MapOptions o = samn::DefaultMapOptions();
    o.scene_feel = samn::SceneFeelOral;
    const double oral_suc = MeanFrames(Curve(actions, o)).second;
    CHECK_MESSAGE(oral_suc > old_suc, "oral suction ", oral_suc, " should be above position suction ", old_suc);
}

TEST_CASE("feel: vibration follows suction") {  // TestVibrationFollowsSuction
    MapOptions o = samn::DefaultMapOptions();
    o.scene_feel = samn::SceneFeelIntercourse;
    o.vibration_follows_suction = true;
    o.smoothing = 0;
    auto frames = Curve(StrokeActions(), o);
    REQUIRE(!frames.empty());
    for (const auto& f : frames)
        CHECK_MESSAGE(std::fabs(f.vibration - f.suction) <= 1e-9, "vibration ", f.vibration,
                      " does not follow suction ", f.suction, " at ", f.at);
}

TEST_CASE("feel: balance") {  // TestBalance
    const auto actions = StrokeActions();
    MapOptions o = samn::DefaultMapOptions();
    o.scene_feel = samn::SceneFeelOral;
    o.vibration_follows_suction = true;
    o.balance = 1;
    auto [vib, suc] = MeanFrames(Curve(actions, o));
    CHECK_MESSAGE(vib <= 1e-9, "balance +1: vib ", vib);
    CHECK_MESSAGE(suc > 0, "balance +1: suc ", suc);
    o.balance = -1;
    std::tie(vib, suc) = MeanFrames(Curve(actions, o));
    CHECK_MESSAGE(suc <= 1e-9, "balance -1: suc ", suc);
    CHECK_MESSAGE(vib > 0, "balance -1: vib ", vib);
    o.balance = 0.5;
    const auto [v_half, s_half] = MeanFrames(Curve(actions, o));
    o.balance = 0;
    const auto [v_zero, s_zero] = MeanFrames(Curve(actions, o));
    CHECK_MESSAGE(std::fabs(v_half - v_zero * 0.5) <= 1e-6, "balance 0.5: vib ", v_half, " want ", v_zero * 0.5);
    CHECK_MESSAGE(std::fabs(s_half - s_zero) <= 1e-6, "balance 0.5: suc ", s_half, " want ", s_zero);
    const auto [nv, ns] = samn::BalanceScales(std::numeric_limits<double>::quiet_NaN());
    CHECK(nv == 1);
    CHECK(ns == 1);
}

// ---------------------------------------------------------------------------
// axes_test.go
// ---------------------------------------------------------------------------

TEST_CASE("axes: bake from the recipe and play from explicit axes") {  // TestBakeAxesAndExplicitPlayback
    const std::vector<Action> actions{{0, 20}, {1000, 90}, {2000, 20}};
    MapOptions opts = samn::RecipeFor(samn::ProfileTj);
    opts.contact_vibration = true;
    opts.contact_vibration_span = 0.5;
    opts.tick_ms = 50;
    opts.smoothing = 0;  // deterministic bake

    auto [vib_axis, suc_axis] = samn::BakeAxesFromRecipe(actions, opts, 100);
    REQUIRE(suc_axis.size() >= 2);
    REQUIRE(vib_axis.size() >= 2);
    // Mid contact: suction is high; vibration is above 0 with span 0.5.
    const double mid_suc = samn::SampleAxisPos(suc_axis, 1000);
    const double mid_vib = samn::SampleAxisPos(vib_axis, 1000);
    CHECK_MESSAGE(mid_suc >= 80, "suction at the peak position: ", mid_suc);
    CHECK_MESSAGE(mid_vib > 0, "vibration at the contact peak: ", mid_vib);

    samn::FunscriptView s;
    s.actions = actions;
    s.metadata.profile = samn::ProfileTj;
    samn::DeviceRecipe dr;
    dr.sync = samn::SyncModeName(samn::SyncMode::SuctionPosition);
    dr.playback_source = samn::PlaybackAxes;
    dr.contact_vibration = true;
    dr.tick_ms = 50;
    dr.max_speed = 0.5;
    dr.smoothing = 0;
    dr.min_suction = 0.2;
    s.metadata.device_recipe = dr;
    s.metadata.samn_axes = samn::SamnAxes{vib_axis, suc_axis};

    const MapOptions map_opts = samn::MapOptionsFromScript(s);
    REQUIRE_MESSAGE(map_opts.use_explicit_axes, "expected use_explicit_axes from playback_source=axes");
    auto frames = Curve(s.actions, map_opts);
    REQUIRE(!frames.empty());
    bool found_vib = false;
    for (const auto& f : frames) {
        if (f.vibration > 0.1) found_vib = true;
        CHECK_MESSAGE((f.suction >= 0 && f.suction <= 1), "suction out of range at ", f.at, ": ", f.suction);
    }
    CHECK_MESSAGE(found_vib, "explicit axes playback never raised vibration");
}

TEST_CASE("axes: recipe mode ignores the stored axes") {  // TestRecipeModeIgnoresAxes
    samn::FunscriptView s;
    s.actions = {{0, 20}, {1000, 90}};
    s.metadata.profile = samn::ProfileTj;
    samn::DeviceRecipe dr;
    dr.sync = samn::SyncModeName(samn::SyncMode::SuctionPosition);
    dr.playback_source = samn::PlaybackRecipe;
    dr.tick_ms = 50;
    dr.smoothing = 0;
    s.metadata.device_recipe = dr;
    s.metadata.samn_axes = samn::SamnAxes{{{0, 100}, {1000, 100}}, {{0, 0}, {1000, 0}}};

    const MapOptions opts = samn::MapOptionsFromScript(s);
    CHECK_FALSE(opts.use_explicit_axes);
    auto frames = Curve(s.actions, opts);
    REQUIRE(!frames.empty());
    for (const auto& f : frames) {
        CHECK_MESSAGE(f.vibration == 0, "tj without contact keeps vibration 0 in recipe mode, at ", f.at);
        CHECK_MESSAGE(f.suction > 0, "expected suction from the position, at ", f.at);
    }
}

// ---------------------------------------------------------------------------
// inverted_playback_test.go
// ---------------------------------------------------------------------------

TEST_CASE("inverted: playback actions flip when inverted") {  // TestPlaybackActionsFlipsWhenInverted
    samn::FunscriptView s;
    s.actions = {{0, 10}, {1000, 90}};
    s.inverted = true;
    const auto got = s.PlaybackActions();
    REQUIRE(got.size() == 2);
    CHECK((got[0].at == 0 && got[0].pos == 90));
    CHECK((got[1].at == 1000 && got[1].pos == 10));
    CHECK(s.actions[0].pos == 10);  // stored actions stay as they were
    CHECK(s.actions[1].pos == 90);
    // The free function flips the same way and clamps.
    const std::vector<Action> in{{0, 10}, {1000, 90}};
    const auto flipped = samn::PlaybackActions(in, true);
    REQUIRE(flipped.size() == 2);
    CHECK(flipped[0].pos == 90);
    CHECK(flipped[1].pos == 10);
    CHECK(in[0].pos == 10);
}

TEST_CASE("inverted: playback actions pass through when not inverted") {  // TestPlaybackActionsPassthroughWhenNotInverted
    samn::FunscriptView s;
    s.actions = {{0, 10}, {1000, 90}};
    const auto got = s.PlaybackActions();
    REQUIRE(got.size() == 2);
    CHECK(got[0].pos == 10);
    CHECK(got[1].pos == 90);
    CHECK(got[1].at == 1000);
}

TEST_CASE("inverted: the intensity curve honours the inverted flag") {  // TestToIntensityCurveHonorsInverted
    const std::vector<Action> plain{{0, 0}, {200, 100}, {400, 0}};
    const std::vector<Action> inv{{0, 100}, {200, 0}, {400, 100}};
    MapOptions opts = samn::DefaultMapOptions();
    opts.smoothing = 0;
    opts.min_vibration = 0;
    auto a = Curve(plain, opts);
    auto b = samn::ToIntensityCurve(inv, opts, true);
    REQUIRE(b.has_value());
    REQUIRE(!a.empty());
    REQUIRE(a.size() == b->size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK_MESSAGE(a[i].at == (*b)[i].at, "at mismatch at ", i);
        CHECK_MESSAGE(a[i].vibration == (*b)[i].vibration, "vibration at ", a[i].at, "ms");
        CHECK_MESSAGE(a[i].suction == (*b)[i].suction, "suction at ", a[i].at, "ms");
    }
}
