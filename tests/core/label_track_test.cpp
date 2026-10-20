// Label tracks for AI Train (D-155 §2.2).
#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>

#include "samn/label_track.h"

using samn::LabelBox;
using samn::LabelKey;
using samn::LabelTrack;

namespace {
LabelKey Box(int64_t ms, double x, bool confirmed = true, const char* source = "user") {
    LabelKey k;
    k.ms = ms;
    k.box = {x, 10, 20, 20};
    k.confirmed = confirmed;
    k.source = source;
    return k;
}
LabelKey Absent(int64_t ms, bool confirmed = true) {
    LabelKey k;
    k.ms = ms;
    k.absent = true;
    k.confirmed = confirmed;
    return k;
}
}  // namespace

TEST_CASE("a label track interpolates between box keys") {
    LabelTrack t("glans");
    t.SetKey(Box(1000, 100));
    t.SetKey(Box(0, 0));  // out of order: kept sorted
    REQUIRE(t.keys().size() == 2);
    CHECK(t.keys().front().ms == 0);
    CHECK_FALSE(t.At(-10).present);
    const auto mid = t.At(500);
    CHECK(mid.present);
    CHECK(mid.box.x == doctest::Approx(50));
    CHECK(mid.reviewed);
    // After the last key the box is held.
    CHECK(t.At(5000).present);
    CHECK(t.At(5000).box.x == doctest::Approx(100));
}

TEST_CASE("an absent key hides the label until the next box key") {
    LabelTrack t("mouth");
    t.SetKey(Box(0, 0));
    t.SetKey(Absent(1000));
    t.SetKey(Box(2000, 50));
    CHECK(t.At(900).present);
    CHECK(t.At(900).box.x == doctest::Approx(0));  // no sliding into the absent key
    CHECK_FALSE(t.At(1500).present);
    CHECK(t.At(1500).reviewed);  // reviewed and empty: a negative sample
    CHECK(t.At(2000).present);
}

TEST_CASE("reviewed needs confirmed keys on both sides") {
    LabelTrack t("hand_1");
    t.SetKey(Box(0, 0, true));
    t.SetKey(Box(1000, 100, false, "tracker"));
    CHECK_FALSE(t.At(500).reviewed);
    CHECK(t.At(0).reviewed);
    CHECK(t.Confirm(0, 2000) == 1);
    CHECK(t.At(500).reviewed);
}

TEST_CASE("keys are replaced, found and removed within a tolerance") {
    LabelTrack t("glans");
    t.SetKey(Box(1000, 0));
    t.SetKey(Box(1010, 30), 20);  // the same frame: replaced
    REQUIRE(t.keys().size() == 1);
    CHECK(t.keys()[0].box.x == doctest::Approx(30));
    CHECK(t.KeyAt(1015, 20) != nullptr);
    CHECK(t.KeyAt(1100, 20) == nullptr);
    CHECK_FALSE(t.RemoveKey(1100, 20));
    CHECK(t.RemoveKey(1000, 20));
    CHECK(t.keys().empty());
}

TEST_CASE("a label project survives save and load") {
    samn::LabelProject p;
    p.video = "clip.mp4";
    p.duration_ms = 3000;
    auto& g = p.AddTrack("glans");
    g.SetKey(Box(0, 0));
    g.SetKey(Absent(1000, false));
    p.AddTrack("glans");  // no second track of the same label
    p.AddTrack("mouth").SetKey(Box(500, 5, false, "teacher:nudenet"));
    REQUIRE(p.tracks.size() == 2);
    const auto dir = std::filesystem::temp_directory_path() / "samn_label_track_test";
    std::filesystem::create_directories(dir);
    const auto path = samn::LabelProjectPath(dir / "clip.mp4");
    CHECK(path.filename() == "clip.labels.json");
    samn::SaveLabelProject(path, p);
    const auto back = samn::LoadLabelProject(path);
    CHECK(back.video == "clip.mp4");
    CHECK(back.duration_ms == 3000);
    REQUIRE(back.tracks.size() == 2);
    REQUIRE(back.tracks[0].keys().size() == 2);
    CHECK(back.tracks[0].keys()[1].absent);
    CHECK_FALSE(back.tracks[0].keys()[1].confirmed);
    CHECK(back.tracks[1].keys()[0].source == "teacher:nudenet");
    CHECK_THROWS_AS(samn::ParseLabelProject("{}"), std::runtime_error);
    CHECK_THROWS_AS(samn::ParseLabelProject("not json"), std::runtime_error);
    std::filesystem::remove_all(dir);
}

TEST_CASE("samples come from reviewed stretches only, negatives included") {
    samn::LabelProject p;
    p.duration_ms = 4000;
    auto& g = p.AddTrack("glans");
    g.SetKey(Box(0, 0));
    g.SetKey(Box(1000, 100));
    g.SetKey(Absent(2000));
    g.SetKey(Absent(3000));
    p.AddTrack("hand_1").SetKey(Box(0, 0, false, "tracker"));  // never reviewed
    p.AddTrack("other").SetKey(Box(0, 0));                      // not a class
    const auto samples = samn::ExportSamples(p, {"glans", "hand_1"}, 500);
    // 0..4000 every 500 ms; glans is reviewed from 0 to the end (absent 2000..).
    REQUIRE(samples.size() == 9);
    CHECK(samples[1].ms == 500);
    REQUIRE(samples[1].items.size() == 1);
    CHECK(samples[1].items[0].class_id == 0);
    CHECK(samples[1].items[0].box.x == doctest::Approx(50));
    CHECK(samples[6].items.empty());  // 3000 ms: a negative sample
    CHECK(samn::ExportSamples(p, {"glans"}, 0).empty());
}

TEST_CASE("a class has as many objects as the scene needs") {
    CHECK(samn::LabelClassOf("hand 2") == "hand");
    CHECK(samn::LabelClassOf("hand") == "hand");
    CHECK(samn::LabelClassOf("top 10 x") == "top 10 x");
    samn::LabelProject p;
    p.duration_ms = 1000;
    CHECK(p.AddObject("hand").label() == "hand");
    CHECK(p.AddObject("hand").label() == "hand 2");
    CHECK(p.AddObject("nipple").label() == "nipple");
    CHECK(p.AddObject("hand").label() == "hand 3");
    CHECK(p.ObjectCount("hand") == 3);
    CHECK(p.Classes() == std::vector<std::string>{"hand", "nipple"});
    // Two hands at once: one sample, two boxes of class 0.
    p.Track("hand")->SetKey(Box(0, 0));
    p.Track("hand 2")->SetKey(Box(0, 50));
    const auto s = samn::ExportSamples(p, p.Classes(), 1000);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].items.size() == 2);
    CHECK(s[0].items[0].class_id == 0);
    CHECK(s[0].items[1].class_id == 0);
    // The class survives save and load.
    const auto back = samn::ParseLabelProject(samn::SerializeLabelProject(p));
    CHECK(back.tracks[1].cls() == "hand");
    CHECK(back.tracks[1].label() == "hand 2");
}

TEST_CASE("a covered object leaves its frames out of the samples") {
    samn::LabelProject p;
    p.duration_ms = 2000;
    auto& g = p.AddTrack("glans");
    g.SetKey(Box(0, 0));
    LabelKey covered = Absent(1000);
    covered.covered = true;
    g.SetKey(covered);
    CHECK(g.At(1500).covered);
    CHECK_FALSE(g.At(1500).present);
    const auto s = samn::ExportSamples(p, {"glans"}, 500);
    REQUIRE(s.size() == 2);  // 0 and 500 only; 1000..2000 are covered
    CHECK(s.back().ms == 500);
    const auto back = samn::ParseLabelProject(samn::SerializeLabelProject(p));
    CHECK(back.tracks[0].keys()[1].covered);
}

TEST_CASE("exclusion zones are a class of their own") {
    CHECK(samn::IsExcludeClass("exclude"));
    CHECK_FALSE(samn::IsExcludeClass("hand"));
    samn::LabelProject p;
    p.duration_ms = 1000;
    p.AddObject("glans").SetKey(Box(0, 0));
    p.AddObject(samn::kExcludeClass).SetKey(Box(0, 40));
    p.AddObject(samn::kExcludeClass).SetKey(Box(0, 80));
    p.Track("exclude 2")->SetKey(Absent(500));
    CHECK(p.Classes() == std::vector<std::string>{"glans", "exclude"});
    // The Generator sees both zones at first, then only the one still there.
    REQUIRE(samn::ExcludeZonesAt(p, 0).size() == 2);
    REQUIRE(samn::ExcludeZonesAt(p, 600).size() == 1);
    CHECK(samn::ExcludeZonesAt(p, 600)[0].x == doctest::Approx(40));
    // Exported as class 1: the model learns "this is not it".
    const auto s = samn::ExportSamples(p, p.Classes(), 1000);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].items.size() == 3);
    CHECK(s[1].items.size() == 2);  // exclude 2 has left the picture
    CHECK(s[0].items[1].class_id == 1);
    CHECK(s[0].items[2].class_id == 1);
}

TEST_CASE("phases cut back, split and merge like chapters") {
    samn::LabelProject p;
    p.duration_ms = 60000;
    CHECK(samn::PhaseNames().front() == "intro");
    p.SetPhase(0, 10000, "intro");
    p.SetPhase(10000, 50000, "oral");
    p.SetPhase(50000, 60000, "climax");
    REQUIRE(p.phases.size() == 3);
    CHECK(p.PhaseAt(5000)->name == "intro");
    CHECK(p.PhaseAt(50000)->name == "climax");
    CHECK(p.PhaseAt(60000) == nullptr);
    // A hand stretch inside the oral one splits it.
    p.SetPhase(20000, 30000, "hand");
    REQUIRE(p.phases.size() == 5);
    CHECK(p.PhaseAt(15000)->name == "oral");
    CHECK(p.PhaseAt(25000)->name == "hand");
    CHECK(p.PhaseAt(35000)->name == "oral");
    // Back to oral: the three merge again.
    p.SetPhase(20000, 30000, "oral");
    REQUIRE(p.phases.size() == 3);
    CHECK(p.phases[1].start_ms == 10000);
    CHECK(p.phases[1].end_ms == 50000);
    // A suggestion stays apart from a confirmed neighbour; clearing leaves a hole.
    p.SetPhase(45000, 50000, "climax", false, "rules");
    CHECK(p.PhaseAt(47000)->source == "rules");
    CHECK_FALSE(p.PhaseAt(47000)->confirmed);
    p.SetPhase(0, 5000, "");
    CHECK(p.PhaseAt(1000) == nullptr);
    CHECK(p.PhaseAt(6000)->name == "intro");
    const auto back = samn::ParseLabelProject(samn::SerializeLabelProject(p));
    REQUIRE(back.phases.size() == p.phases.size());
    CHECK(back.PhaseAt(47000)->source == "rules");
    CHECK_FALSE(back.PhaseAt(47000)->confirmed);
    CHECK(back.PhaseAt(25000)->name == "oral");
}

TEST_CASE("YOLO lines are normalised and clipped to the frame") {
    samn::LabelSample s;
    s.items.push_back({2, LabelBox{90, 40, 20, 20}});  // runs over the right edge
    s.items.push_back({0, LabelBox{200, 200, 5, 5}});  // fully outside
    CHECK(samn::YoloLines(s, 100, 100) == "2 0.950000 0.500000 0.100000 0.200000\n");
}

TEST_CASE("tracking between keys: forward and backward blend, disagreement is uncertain") {
    using samn::TrackSample;
    // Both runs agree at first; later the forward one drifts away.
    std::vector<TrackSample> fwd{{0, LabelBox{0, 10, 20, 20}},
                                 {250, LabelBox{10, 10, 20, 20}},
                                 {500, LabelBox{20, 10, 20, 20}},
                                 {750, LabelBox{90, 10, 20, 20}},
                                 {1000, LabelBox{40, 10, 20, 20}}};
    std::vector<TrackSample> bwd{{250, LabelBox{10, 10, 20, 20}},
                                 {500, LabelBox{22, 10, 20, 20}},
                                 {750, std::nullopt},
                                 {1000, LabelBox{40, 10, 20, 20}}};
    const auto keys = samn::BlendTracks(fwd, bwd, 0, 1000);
    REQUIRE(keys.size() == 3);  // strictly between the two keys
    CHECK(keys[0].ms == 250);
    CHECK_FALSE(keys[0].confirmed);
    CHECK(keys[0].source == "tracker");
    CHECK_FALSE(keys[0].uncertain);
    CHECK(keys[1].box.x == doctest::Approx(21.0));  // halfway: half of each
    CHECK_FALSE(keys[1].uncertain);
    CHECK(keys[2].uncertain);  // the backward run lost it
    CHECK(keys[2].box.x == doctest::Approx(90.0));
    CHECK(samn::BoxIoU({0, 0, 10, 10}, {0, 0, 10, 10}) == doctest::Approx(1.0));
    CHECK(samn::BoxIoU({0, 0, 10, 10}, {20, 0, 10, 10}) == doctest::Approx(0.0));

    samn::LabelProject p;
    p.duration_ms = 1000;
    auto& t = p.AddTrack("glans");
    t.SetKey(Box(0, 0));
    t.SetKey(Box(1000, 40));
    t.SetKey(Box(600, 5, false, "tracker"));  // an old guess: replaced
    CHECK(samn::FillBetweenKeys(t, 0, 1000, keys) == 3);
    CHECK(t.keys().size() == 5);
    CHECK(t.KeyAt(600) == nullptr);
    CHECK(samn::NextUncertain(p, 0) == 750);
    CHECK_FALSE(samn::NextUncertain(p, 750).has_value());
    CHECK_FALSE(samn::NextUncertain(p, 0, "hand").has_value());
    // The flag survives save and load.
    const auto back = samn::ParseLabelProject(samn::SerializeLabelProject(p));
    CHECK(back.tracks[0].KeyAt(750)->uncertain);
    // A confirmed key in the gap stays.
    t.SetKey(Box(500, 25));
    samn::FillBetweenKeys(t, 0, 1000, keys);
    CHECK(t.KeyAt(500)->confirmed);
    CHECK(t.KeyAt(500)->box.x == doctest::Approx(25.0));
}
