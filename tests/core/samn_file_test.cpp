// Port of tests/test_samn_file.py (SamNPlayer convert_test.go, export_merge_test.go
// plus round-trip and Go byte-compatibility tests) for core/src/samn_file.cpp.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "samn/samn_file.h"

namespace fs = std::filesystem;
using namespace samn;

namespace {

// Written by the Go package (samn.Save / Document.ExportFunscript) for the
// document built in GoldenDocument() / the target in kGoldenOldTarget.
const std::string kGoGoldenSamn = R"GOLD({
  "version": 1,
  "kind": "samnplayer.script",
  "creator": "te\u003cs\u003et \u0026 ü",
  "durationMs": 1000,
  "videoPath": "/v/a.mp4",
  "profile": "tj",
  "playbackSource": "recipe",
  "recipe": {
    "sync": "suction_position",
    "min_suction": 0.2,
    "tick_ms": 50,
    "max_speed": 0.5,
    "smoothing": 0.22,
    "contact_vibration": true,
    "contact_vibration_span": 0.5,
    "contact_vibration_curve": "soft",
    "feel_balance": 1e-7,
    "scene_ranges": [
      {
        "start_ms": 0,
        "end_ms": 10,
        "feel": "oral"
      }
    ]
  },
  "general": [
    {
      "at": 0,
      "pos": 20
    },
    {
      "at": 1000,
      "pos": 100
    }
  ],
  "vibration": [
    {
      "at": 0,
      "pos": 1
    }
  ],
  "chapters": [
    {
      "name": "A",
      "startTime": 0,
      "endTime": 5
    }
  ],
  "bookmarks": [
    {
      "name": "B",
      "time": 500
    }
  ],
  "oMarkers": [
    {
      "startMs": 1,
      "endMs": 2,
      "kind": "primary",
      "intensity": 1
    }
  ],
  "trackingGaps": [
    {
      "start_ms": 1,
      "end_ms": 2
    }
  ],
  "strengthPresets": [
    {
      "name": "soft",
      "vibrationScale": 0.7,
      "suctionScale": 0.85,
      "contactSpan": 0.85,
      "contactCurve": "soft"
    },
    {
      "name": "normal",
      "vibrationScale": 1,
      "suctionScale": 1
    },
    {
      "name": "strong",
      "vibrationScale": 1.3,
      "suctionScale": 1.1,
      "contactSpan": 0.55,
      "contactCurve": "peak"
    }
  ],
  "activeStrength": "normal",
  "contactMarks": {
    "tip": {
      "x": 1,
      "y": 2,
      "w": 3,
      "h": 4,
      "class": "c"
    },
    "extras": [
      {
        "x": 1,
        "y": 0,
        "w": 0,
        "h": 0
      }
    ],
    "drive_stroke": false
  },
  "trajectory": {
    "width": 10,
    "height": 20,
    "tip": [
      {
        "atMs": 1,
        "x": 1.5,
        "y": 2
      }
    ]
  },
  "sceneMap": {
    "version": 1,
    "video": {
      "durationMs": 0,
      "width": 0,
      "height": 0
    },
    "grid": {
      "cols": 0,
      "rows": 0
    },
    "windows": [
      {
        "startMs": 0,
        "endMs": 5,
        "tempoHz": 1.25,
        "score_b64": "AAE=",
        "chosen": -1,
        "box": [
          1,
          2.5
        ]
      }
    ]
  },
  "qualityScore": 0.5,
  "qualityPassed": true,
  "qualityWarnings": [
    "w"
  ]
})GOLD";
const std::string kGoGoldenExport = R"GOLD({
  "actions": [
    {
      "at": 0,
      "pos": 20
    },
    {
      "at": 1000,
      "pos": 100
    }
  ],
  "metadata": {
    "ai_opinion": {
      "verdict": "ok",
      "reason": "r"
    },
    "bookmarks": [
      {
        "name": "B",
        "time": 500
      }
    ],
    "chapters": [
      {
        "name": "A",
        "startTime": 0,
        "endTime": 5
      }
    ],
    "contact_marks": {
      "tip": {
        "x": 1,
        "y": 2,
        "w": 3,
        "h": 4,
        "class": "c"
      },
      "extras": [
        {
          "x": 1,
          "y": 0,
          "w": 0,
          "h": 0
        }
      ],
      "drive_stroke": false
    },
    "creator": "te\u003cs\u003et \u0026 ü",
    "device_recipe": {
      "sync": "suction_position",
      "min_suction": 0.2,
      "tick_ms": 50,
      "max_speed": 0.5,
      "smoothing": 0.22,
      "contact_vibration": true,
      "contact_vibration_span": 0.5,
      "contact_vibration_curve": "soft",
      "feel_balance": 1e-7,
      "scene_ranges": [
        {
          "start_ms": 0,
          "end_ms": 10,
          "feel": "oral"
        }
      ]
    },
    "duration": 1000,
    "oMarkers": [
      {
        "startMs": 1,
        "endMs": 2,
        "kind": "primary",
        "intensity": 1
      }
    ],
    "profile": "tj",
    "quality_passed": true,
    "quality_score": 0.5,
    "quality_warnings": [
      "w"
    ],
    "tags": [
      "a"
    ],
    "title": "T",
    "tracking_gaps": [
      {
        "start_ms": 1,
        "end_ms": 2
      }
    ],
    "trajectory": {
      "width": 10,
      "height": 20,
      "tip": [
        {
          "atMs": 1,
          "x": 1.5,
          "y": 2
        }
      ]
    }
  },
  "range": 90.0,
  "version": "1.0",
  "z": {
    "b": [
      1.50,
      2e3
    ],
    "a": "\u003cx\u003e"
  }
})GOLD";
const std::string kGoldenOldTarget =
    R"GOLD({"actions":[{"at":0,"pos":0}],"inverted":true,"range":90.0,"version":"1.0",)GOLD"
    R"GOLD("z":{"b":[1.50,2e3],"a":"<x>"},"metadata":{"title":"T","duration":1.0,"tags":["a"],)GOLD"
    R"GOLD("ai_opinion":{"verdict":"ok","reason":"r"},"samn_axes":{"vibration":[]}}})GOLD";

// Per-test temp directory (pytest's tmp_path), removed on destruction.
struct TmpDir {
    fs::path dir;
    TmpDir() {
        static std::atomic<unsigned> n{0};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("samn_file_" + std::to_string(stamp) + "_" + std::to_string(n++));
        fs::create_directories(dir);
    }
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path operator/(const std::string& name) const { return dir / name; }
};

void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary);
    out << s;
}

std::string ReadText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> ListDir(const fs::path& p) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(p)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<Action> Acts(std::initializer_list<std::pair<int64_t, int>> l) {
    std::vector<Action> v;
    for (const auto& p : l) v.push_back({p.first, p.second});
    return v;
}

bool SameActions(const std::vector<Action>& a, const std::vector<Action>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].at != b[i].at || a[i].pos != b[i].pos) return false;
    return true;
}

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

Document GoldenDocument() {
    Document d;
    d.creator = "te<s>t & \xC3\xBC";
    d.video_path = "/v/a.mp4";
    d.profile = "tf";
    d.recipe.sync = "suction_position";
    d.recipe.min_suction = 0.2;
    d.recipe.tick_ms = 50;
    d.recipe.max_speed = 0.5;
    d.recipe.smoothing = 0.22;
    d.recipe.contact_vibration = true;
    d.recipe.contact_vibration_span = 0.5;
    d.recipe.contact_vibration_curve = "soft";
    d.recipe.feel_balance = 1e-7;
    d.recipe.scene_ranges = {SceneFeelRange{0, 10, "oral"}};
    d.general = Acts({{1000, 150}, {0, 20}});
    d.vibration = Acts({{0, 1}});
    d.chapters = {Chapter{"A", 0, 5}};
    d.bookmarks = {Bookmark{"B", 500}};
    OMarker om;
    om.start_ms = 1;
    om.end_ms = 2;
    om.kind = "primary";
    om.intensity = 1.0;
    d.o_markers = {om};
    d.tracking_gaps = {TrackingGap{1, 2}};
    d.strength_presets = DefaultStrengthPresets();
    d.active_strength = "normal";
    ContactMarks cm;
    ContactMarkBox tip;
    tip.x = 1;
    tip.y = 2;
    tip.w = 3;
    tip.h = 4;
    tip.cls = "c";
    cm.tip = tip;
    ContactMarkBox extra;
    extra.x = 1;
    cm.extras = {extra};
    d.contact_marks = cm;
    TrajectoryData tr;
    tr.width = 10;
    tr.height = 20;
    TrajectoryPoint tp;
    tp.at_ms = 1;
    tp.x = 1.5;
    tp.y = 2.0;
    tr.tip = {tp};
    d.trajectory = tr;
    SceneMapData sm;
    sm.version = 1;
    SceneMapWindow w;
    w.start_ms = 0;
    w.end_ms = 5;
    w.tempo_hz = 1.25;
    w.score_b64 = "AAE=";
    w.chosen = -1;
    w.box = {1.0, 2.5};
    sm.windows = {w};
    d.scene_map = sm;
    d.quality_score = 0.5;
    d.quality_passed = true;
    d.quality_warnings = {"w"};
    return d;
}

}  // namespace

// --- convert_test.go ---------------------------------------------------------

TEST_CASE("samn_file: round trip funscript <-> samn") {  // TestRoundTripFunscriptSamn
    TmpDir tmp;
    const fs::path fs_path = tmp / "clip.funscript";
    WriteText(fs_path, R"JSON({
  "actions": [{"at":0,"pos":20},{"at":1000,"pos":90},{"at":2000,"pos":20}],
  "metadata": {
    "creator": "test",
    "duration": 2000,
    "profile": "tj",
    "device_recipe": {
      "sync": "suction_position",
      "min_suction": 0.2,
      "tick_ms": 50,
      "max_speed": 0.5,
      "smoothing": 0.22,
      "contact_vibration": true,
      "contact_vibration_span": 0.5,
      "contact_vibration_curve": "soft"
    },
    "chapters": [{"name": "A", "startTime": 0, "endTime": 1000}],
    "bookmarks": [{"name": "B", "time": 500}]
  }
})JSON");

    Document d = LoadFunscriptFile(fs_path);
    CHECK(d.general.size() == 3);
    CHECK(d.chapters.size() == 1);
    CHECK(d.bookmarks.size() == 1);
    OMarker om;
    om.start_ms = 800;
    om.end_ms = 1200;
    om.kind = OMarkerPrimary;
    om.intensity = 1;
    d.o_markers = {om};
    d.BakeNeoAxes();
    CHECK(d.playback_source == PlaybackAxes);
    CHECK(d.vibration.size() >= 2);
    CHECK(d.suction.size() >= 2);
    d.strength_presets = DefaultStrengthPresets();
    d.active_strength = "normal";

    const fs::path samn_path = tmp / "clip.samn";
    SaveDocument(samn_path, d);
    Document got = LoadDocument(samn_path);
    CHECK(got.active_strength == "normal");
    CHECK(got.strength_presets.size() == 3);

    // Player bridge
    FunscriptView script = got.ToFunscript();
    MapOptions opts = MapOptionsFromScript(script);
    CHECK_MESSAGE(opts.use_explicit_axes, "expected axes playback from .samn");
    auto frames = ToIntensityCurve(script.actions, opts, script.inverted);
    REQUIRE(frames.has_value());
    CHECK_FALSE(frames->empty());

    // Export must not embed samn_axes
    const fs::path out_fs = tmp / "out.funscript";
    got.ExportFunscript(out_fs);
    const std::string raw = ReadText(out_fs);
    CHECK_FALSE(Contains(raw, "samn_axes"));
    FunscriptView reloaded = LoadFunscript(out_fs);
    CHECK(reloaded.actions.size() == 3);
    auto oms = LoadOMarkers(out_fs);
    CHECK(oms.size() == 1);
}

TEST_CASE("samn_file: is_samn_path") {  // TestIsSamnPath
    CHECK((IsSamnPath("a.samn") && !IsSamnPath("a.funscript")));
    CHECK(CompanionSamnPath("x.funscript") == "x.samn");
    // same rules as Go filepath.Ext
    CHECK(IsSamnPath("A.SAMN"));
    CHECK(CompanionSamnPath("dir.d/noext") == "dir.d/noext.samn");
    CHECK(CompanionFunscriptPath("/a/b.samn") == "/a/b.funscript");
}

// --- export_merge_test.go ----------------------------------------------------

TEST_CASE("samn_file: export_funscript keeps foreign metadata") {  // TestExportFunscriptKeepsForeignMetadata
    TmpDir tmp;
    const fs::path p = tmp / "x.funscript";
    WriteText(p, R"JSON({"actions":[{"at":0,"pos":0},{"at":500,"pos":100}],"inverted":false,"range":90,"version":"1.0",
	 "metadata":{"title":"My scene","tags":["a"],"performers":["p"],"script_url":"u","duration":1.0,
	  "audio_check":{"script_hz":1.5,"audio_hz":1.4,"warnings":[],"speech_hold_ms":500,
	   "segments":[{"label":"gentle","start_ms":0,"end_ms":400}]},
	  "ai_opinion":{"verdict":"ok","reason":"r"},
	  "tracking_gaps":[{"start_ms":100,"end_ms":200}],
	  "samn_axes":{"vibration":[{"at":0,"pos":10}]}}})JSON");
    Document d;
    d.general = Acts({{0, 5}, {1000, 95}});
    d.ExportFunscript(p);
    const auto top = nlohmann::json::parse(ReadText(p));
    for (const char* k : {"range", "version"})
        CHECK_MESSAGE(top.contains(k), "top-level " << k << " lost");
    const auto& meta = top["metadata"];
    for (const char* k : {"title", "tags", "performers", "script_url", "audio_check", "ai_opinion"})
        CHECK_MESSAGE(meta.contains(k), "metadata " << k << " lost");
    // What the export manages follows the document - also by omission.
    for (const char* k : {"tracking_gaps", "samn_axes"})
        CHECK_MESSAGE(!meta.contains(k), "metadata " << k << " must follow the document (dropped)");
    // Exported points are already as played: a kept "inverted" would flip twice.
    CHECK_MESSAGE(!top.contains("inverted"), "top-level \"inverted\" must not survive the export");
    FunscriptView s = LoadFunscript(p);
    REQUIRE(s.actions.size() == 2);
    CHECK_MESSAGE((s.actions[1].at == 1000 && s.actions[1].pos == 95), "actions not replaced");
    const auto& ac = meta["audio_check"];
    CHECK(ac["segments"].size() == 1);
    CHECK_MESSAGE(s.metadata.duration == 1000, "audio_check duration");
}

TEST_CASE("samn_file: export_funscript without usable target") {  // TestExportFunscriptWithoutUsableTarget
    TmpDir tmp;
    Document d;
    d.general = Acts({{0, 5}, {1000, 95}});
    for (const std::string name : {"new.funscript", "broken.funscript"}) {
        const fs::path p = tmp / name;
        if (name == "broken.funscript") WriteText(p, "{not json");
        d.ExportFunscript(p);
        LoadFunscript(p);
        CHECK_MESSAGE(!fs::exists(p.string() + ".tmp"), name << ": temp file left behind");
    }
    CHECK(ListDir(tmp.dir) == std::vector<std::string>{"broken.funscript", "new.funscript"});
}

TEST_CASE("samn_file: from_funscript bakes inverted") {  // TestFromFunscriptBakesInverted
    FunscriptView s = ParseFunscript(R"({"actions":[{"at":0,"pos":10},{"at":1000,"pos":80}],"inverted":true})");
    Document d = FromFunscript(s, "");
    REQUIRE(d.general.size() == 2);
    CHECK_MESSAGE((d.general[0].pos == 90 && d.general[1].pos == 20), "want played positions 90/20");
}

// --- additions: round trip, Go byte compatibility, rules ---------------------

TEST_CASE("samn_file: write is byte identical to Go") {
    TmpDir tmp;
    const fs::path p = tmp / "g.samn";
    Document d = GoldenDocument();
    SaveDocument(p, d);
    CHECK(ReadText(p) == kGoGoldenSamn);
}

TEST_CASE("samn_file: export is byte identical to Go") {
    TmpDir tmp;
    const fs::path p = tmp / "t.funscript";
    WriteText(p, kGoldenOldTarget);
    GoldenDocument().ExportFunscript(p);
    CHECK(ReadText(p) == kGoGoldenExport);
}

TEST_CASE("samn_file: Go file reads and rewrites identically") {
    Document d = ParseDocument(kGoGoldenSamn);
    CHECK_MESSAGE(SameActions(d.general, Acts({{0, 20}, {1000, 100}})), "sorted + clamped by Normalize");
    CHECK(d.profile == "tj");
    CHECK(d.playback_source == "recipe");
    CHECK(d.duration_ms == 1000);
    REQUIRE(d.scene_map.has_value());
    REQUIRE(d.scene_map->windows.size() == 1);
    CHECK(d.scene_map->windows[0].chosen == -1);
    CHECK(MarshalDocument(d) == kGoGoldenSamn);
}

TEST_CASE("samn_file: write then read round trip") {
    TmpDir tmp;
    const fs::path p = tmp / "rt.samn";
    Document d = GoldenDocument();
    SaveDocument(p, d);  // normalizes d in place, like Go
    Document got = LoadDocument(p);
    CHECK(got == d);
    CHECK(SameActions(got.general, Acts({{0, 20}, {1000, 100}})));
    CHECK(ListDir(tmp.dir) == std::vector<std::string>{"rt.samn"});  // no temp file left behind
}

TEST_CASE("samn_file: JSON keys and order match Go") {
    TmpDir tmp;
    const fs::path p = tmp / "k.samn";
    Document d;
    d.general = Acts({{0, 5}, {1000, 95}});
    SaveDocument(p, d);
    const auto top = nlohmann::ordered_json::parse(ReadText(p));
    auto keys = [](const nlohmann::ordered_json& j) {
        std::vector<std::string> k;
        for (auto it = j.begin(); it != j.end(); ++it) k.push_back(it.key());
        return k;
    };
    // minimal document: omitempty drops everything but these (Go struct order)
    CHECK(keys(top) == std::vector<std::string>{"version", "kind", "durationMs", "profile", "playbackSource",
                                                "recipe", "general"});
    CHECK(keys(top["recipe"]) ==
          std::vector<std::string>{"sync", "min_suction", "tick_ms", "max_speed", "smoothing"});
    CHECK(top["general"][0] == nlohmann::ordered_json::parse(R"({"at": 0, "pos": 5})"));
    CHECK(keys(nlohmann::ordered_json::parse(kGoGoldenSamn)) ==
          keys(nlohmann::ordered_json::parse(MarshalDocument(ParseDocument(kGoGoldenSamn)))));
}

TEST_CASE("samn_file: unknown fields are dropped like Go") {
    Document d = ParseDocument(R"({"general":[{"at":0,"pos":1.6}],"future":{"x":1},"Creator":"c"})");
    CHECK(SameActions(d.general, Acts({{0, 2}})));  // float pos rounded; keys match case-insensitively
    CHECK(d.creator == "c");
    CHECK_FALSE(Contains(MarshalDocument(d), "future"));
}

TEST_CASE("samn_file: invalid documents") {
    struct Case {
        const char* text;
        const char* msg;
    };
    const Case cases[] = {
        {"{nope", "samn: invalid JSON"},
        {R"({"general":[]})", "samn: general curve is required"},
        {R"({"version":2,"general":[{"at":0,"pos":1}]})", "samn: unsupported version 2 (want 1)"},
        {R"({"kind":"x","general":[{"at":0,"pos":1}]})", "samn: unexpected kind \"x\""},
        {R"({"general":[{"at":-1,"pos":1}]})", "negative time"},
        {R"({"general":[{"at":0,"pos":1}],"oMarkers":[{"startMs":5,"endMs":5,"kind":"primary"}]})",
         "samn: oMarkers[0]: funscript: O-marker end"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        try {
            ParseDocument(c.text);
            FAIL("expected SamnError");
        } catch (const SamnError& e) {
            CHECK_MESSAGE(Contains(e.what(), c.msg), "got: " << e.what());
        }
    }
}

TEST_CASE("samn_file: prefer_samn_companion") {  // cmd/gui-wails preferSamnCompanion
    TmpDir tmp;
    const fs::path fun = tmp / "a.funscript";
    WriteText(fun, "{}");
    const std::string funs = fun.string();
    const std::string samn = (tmp / "a.samn").string();
    CHECK(PreferSamnCompanion(funs) == funs);  // no companion yet
    fs::create_directory(tmp / "a.samn");      // a directory does not count
    CHECK(PreferSamnCompanion(funs) == funs);
    fs::remove(tmp / "a.samn");
    WriteText(tmp / "a.samn", "{}");
    CHECK(PreferSamnCompanion(funs) == samn);
    CHECK(PreferSamnCompanion(samn) == samn);
    CHECK(PreferSamnCompanion("") == "");
}

// --- 0.7 contact / feel settings and quality metadata ------------------------
// Go keeps these in the recipe of a .funscript (SaveContactRecipe,
// SaveFeelRecipe, cmd/gui-wails SaveContactSettings / SaveFeelSettings). In
// SamNPlayer 2 they live in the recipe of a .samn Document.

namespace {

Document RecipeDoc(const std::string& profile) {
    Document d;
    d.profile = profile;
    d.general = {{0, 20}, {800, 90}};
    return d;
}

}  // namespace

TEST_CASE("samn_file: contact settings are stored in the tj recipe") {
    // 0.7 cmd/gui-wails TestSaveContactSettingsPersistsRecipe
    Document d = RecipeDoc("tj");
    d.recipe.sync = "suction_position";
    d.recipe.min_suction = 0.2;
    d.recipe.tick_ms = 50;
    d.recipe.max_speed = 0.5;
    d.recipe.smoothing = 0.22;
    d.recipe.contact_vibration = true;
    d.recipe.contact_vibration_span = 0.75;
    d.recipe.contact_vibration_curve = "linear";
    d.ApplyContactRecipe(true, 0.5, "soft");
    const DeviceRecipe& dr = d.recipe;
    CHECK(dr.contact_vibration);
    CHECK(dr.contact_vibration_span == 0.5);
    CHECK(dr.contact_vibration_curve == "soft");
    // The recipe survives a save and load.
    Document back = ParseDocument(MarshalDocument(d));
    CHECK(back.recipe.contact_vibration);
    CHECK(back.recipe.contact_vibration_span == 0.5);
    CHECK(back.recipe.contact_vibration_curve == "soft");
}

TEST_CASE("samn_file: contact settings keep sync independent on a stroke profile") {
    // 0.7 cmd/gui-wails TestSaveContactSettingsStrokeProfile
    Document d = RecipeDoc("standard");
    d.recipe.sync = "independent";
    d.recipe.min_suction = 0.1;
    d.recipe.tick_ms = 50;
    d.recipe.max_speed = 0.5;
    d.recipe.smoothing = 0.3;
    d.recipe.contact_vibration = true;
    d.recipe.contact_vibration_span = 0.75;
    d.recipe.contact_vibration_curve = "soft";
    d.ApplyContactRecipe(true, 0.55, "peak");
    CHECK(d.recipe.contact_vibration);
    CHECK(d.recipe.contact_vibration_span == 0.55);
    CHECK(d.recipe.contact_vibration_curve == "peak");
    CHECK(d.recipe.sync == "independent");
}

TEST_CASE("samn_file: contact settings fill an empty stroke sync with independent") {
    // 0.7 cmd/gui-wails TestSaveContactSettingsStrokeEmptySyncFillsIndependent and
    // funscript TestSaveContactRecipeStrokeEmptySyncStaysIndependent
    Document d = RecipeDoc("standard");
    d.recipe.min_suction = 0.1;
    d.recipe.tick_ms = 50;
    REQUIRE(d.recipe.sync.empty());
    d.ApplyContactRecipe(true, 0.4, "linear");
    CHECK_MESSAGE(d.recipe.sync == "independent", "stroke empty sync became \"", d.recipe.sync, "\"");
    CHECK(d.recipe.contact_vibration);
    CHECK(d.recipe.contact_vibration_span == 0.4);

    Document e = RecipeDoc("standard");
    e.recipe.min_suction = 0.1;
    e.recipe.tick_ms = 50;
    e.ApplyContactRecipe(true, 0.5, "soft");
    CHECK(e.recipe.sync == "independent");
    CHECK(e.recipe.sync != "suction_position");
    CHECK(e.recipe.contact_vibration);
}

TEST_CASE("samn_file: contact settings fill an empty tj sync with suction_position") {
    // 0.7 funscript TestSaveContactRecipeTjEmptySyncUsesSuctionPosition
    Document d = RecipeDoc("tj");
    d.recipe.min_suction = 0.2;
    REQUIRE(d.recipe.sync.empty());
    d.ApplyContactRecipe(true, 0.5, "soft");
    CHECK(d.recipe.sync == "suction_position");
}

TEST_CASE("samn_file: contact recipe round trip, switching off clears span and curve") {
    // 0.7 funscript TestSaveContactRecipeRoundtrip
    Document d = RecipeDoc("tj");
    d.general = {{0, 20}, {1000, 90}};
    d.recipe.sync = "suction_position";
    d.recipe.min_suction = 0.2;
    d.recipe.tick_ms = 50;
    d.recipe.max_speed = 0.5;
    d.recipe.smoothing = 0.22;
    d.ApplyContactRecipe(true, 0.55, "soft");
    Document got = ParseDocument(MarshalDocument(d));
    CHECK(got.recipe.contact_vibration);
    CHECK(got.recipe.contact_vibration_span == 0.55);
    CHECK(got.recipe.contact_vibration_curve == ContactCurveSoft);
    CHECK_MESSAGE(got.recipe.sync == "suction_position", "sync lost: ", got.recipe.sync);
    CHECK(got.recipe.min_suction == 0.2);
    CHECK(got.recipe.max_speed == 0.5);
    CHECK(got.recipe.smoothing == 0.22);
    CHECK(got.general.size() == 2);

    // The span goes through EffectiveContactSpan, the curve is normalised.
    d.ApplyContactRecipe(true, 2.0, "weicher");
    CHECK(d.recipe.contact_vibration_span == EffectiveContactSpan(2.0));
    CHECK(d.recipe.contact_vibration_span == DefaultContactVibrationSpan);
    CHECK(d.recipe.contact_vibration_curve == ContactCurveSoft);
    d.ApplyContactRecipe(true, 0.1, "no such curve");
    CHECK(d.recipe.contact_vibration_span == 0.4);
    CHECK(d.recipe.contact_vibration_curve == ContactCurveLinear);

    // Off: span and curve are cleared, the rest of the recipe stays.
    d.ApplyContactRecipe(true, 0.55, "soft");
    d.ApplyContactRecipe(false, 0.55, "soft");
    CHECK_FALSE(d.recipe.contact_vibration);
    CHECK(d.recipe.contact_vibration_span == 0);
    CHECK(d.recipe.contact_vibration_curve.empty());
    CHECK(d.recipe.sync == "suction_position");
    CHECK(d.recipe.min_suction == 0.2);
    CHECK(d.recipe.tick_ms == 50);
    CHECK(d.recipe.max_speed == 0.5);
    CHECK(d.recipe.smoothing == 0.22);
}

TEST_CASE("samn_file: feel settings round trip into the map options") {
    // 0.7 funscript TestSaveFeelRecipeRoundTrip
    std::string json = R"({"actions":[)";
    for (int i = 0; i <= 40; ++i) {
        if (i) json += ",";
        json += R"({"at":)" + std::to_string(i * 400) + R"(,"pos":)" + std::to_string(i % 2 == 1 ? 90 : 10) + "}";
    }
    json += R"(],"metadata":{"creator":"x"}})";
    Document d = FromFunscript(ParseFunscript(json));
    d.ApplyFeelRecipe("blowjob", true, 0.4);
    CHECK(d.recipe.scene_feel == SceneFeelOral);

    Document back = ParseDocument(MarshalDocument(d));
    FunscriptView s = back.ToFunscript();
    const MapOptions o = MapOptionsFromScript(s);
    CHECK(o.scene_feel == SceneFeelOral);
    CHECK(o.vibration_follows_suction);
    CHECK(std::fabs(o.balance - 0.4) <= 1e-9);
    CHECK_MESSAGE(s.metadata.creator == "x", "other metadata lost");
    CHECK(back.creator == "x");

    // Unknown names clear the feel, balance is clamped.
    back.ApplyFeelRecipe("dance", false, 7);
    CHECK(back.recipe.scene_feel.empty());
    CHECK_FALSE(back.recipe.vibration_follows_suction);
    CHECK(back.recipe.feel_balance == 1);
}

TEST_CASE("samn_file: a new feel changes the baked vibration axis") {
    // 0.7 cmd/gui-wails TestSaveFeelSettingsRebakesEmotionScript
    Document d = FromFunscript(ParseFunscript(
        R"({"actions":[{"at":0,"pos":10},{"at":400,"pos":90},{"at":800,"pos":10},{"at":1200,"pos":90}]})"));
    d.BakeNeoAxes();
    const std::vector<Action> before = d.vibration;
    REQUIRE(!before.empty());
    REQUIRE(!d.suction.empty());

    d.ApplyFeelRecipe("oral", true, 0);
    d.BakeNeoAxes();
    REQUIRE(!d.vibration.empty());
    bool same = before.size() == d.vibration.size();
    for (size_t i = 0; same && i < before.size(); ++i)
        same = before[i].at == d.vibration[i].at && before[i].pos == d.vibration[i].pos;
    CHECK_MESSAGE(!same, "baked vibration did not change with the feel");
}

TEST_CASE("samn_file: quality score and verdict are separate fields") {  // TestParseQualityMetadata
    const std::string head = R"({"actions":[{"at":0,"pos":0},{"at":100,"pos":90}])";
    struct Case {
        const char* name;
        std::string json;
        std::optional<double> score;
        std::optional<bool> passed;
    };
    const std::vector<Case> cases{
        {"failed despite a score above 0.5", head + R"(,"metadata":{"quality_score":0.63,"quality_passed":false}})",
         0.63, false},
        {"passed", head + R"(,"metadata":{"quality_score":1.0,"quality_passed":true}})", 1.0, true},
        {"old script without quality_passed", head + R"(,"metadata":{"quality_score":0.8}})", 0.8, std::nullopt},
        {"no quality metadata", head + "}", std::nullopt, std::nullopt},
    };
    for (const auto& c : cases) {
        CAPTURE(c.name);
        FunscriptView s = ParseFunscript(c.json);
        CHECK(s.metadata.quality_score == c.score);
        CHECK(s.metadata.quality_passed == c.passed);
        // The same fields reach a .samn document.
        Document d = FromFunscript(s);
        CHECK(d.quality_score == c.score);
        CHECK(d.quality_passed == c.passed);
    }
}
