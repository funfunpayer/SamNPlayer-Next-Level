"""Port of SamNPlayer samn/convert_test.go and samn/export_merge_test.go,
plus round-trip and Go-byte-compatibility tests for samn/formats/samn_file.py.
"""
import json
import os
import re

import pytest

from samn.formats import samn_file as sf
from samn.formats.mapper import SceneFeelRange, TrackingGap, to_intensity_curve
from samn.formats.samn_file import Document, OMarker, SamnError

# Written by the Go package (samn.Save / Document.ExportFunscript) for the
# document built in _golden_document() / the target in _GOLDEN_OLD_TARGET.
GO_GOLDEN_SAMN = '{\n  "version": 1,\n  "kind": "samnplayer.script",\n  "creator": "te\\u003cs\\u003et \\u0026 ü",\n  "durationMs": 1000,\n  "videoPath": "/v/a.mp4",\n  "profile": "tj",\n  "playbackSource": "recipe",\n  "recipe": {\n    "sync": "suction_position",\n    "min_suction": 0.2,\n    "tick_ms": 50,\n    "max_speed": 0.5,\n    "smoothing": 0.22,\n    "contact_vibration": true,\n    "contact_vibration_span": 0.5,\n    "contact_vibration_curve": "soft",\n    "feel_balance": 1e-7,\n    "scene_ranges": [\n      {\n        "start_ms": 0,\n        "end_ms": 10,\n        "feel": "oral"\n      }\n    ]\n  },\n  "general": [\n    {\n      "at": 0,\n      "pos": 20\n    },\n    {\n      "at": 1000,\n      "pos": 100\n    }\n  ],\n  "vibration": [\n    {\n      "at": 0,\n      "pos": 1\n    }\n  ],\n  "chapters": [\n    {\n      "name": "A",\n      "startTime": 0,\n      "endTime": 5\n    }\n  ],\n  "bookmarks": [\n    {\n      "name": "B",\n      "time": 500\n    }\n  ],\n  "oMarkers": [\n    {\n      "startMs": 1,\n      "endMs": 2,\n      "kind": "primary",\n      "intensity": 1\n    }\n  ],\n  "trackingGaps": [\n    {\n      "start_ms": 1,\n      "end_ms": 2\n    }\n  ],\n  "strengthPresets": [\n    {\n      "name": "soft",\n      "vibrationScale": 0.7,\n      "suctionScale": 0.85,\n      "contactSpan": 0.85,\n      "contactCurve": "soft"\n    },\n    {\n      "name": "normal",\n      "vibrationScale": 1,\n      "suctionScale": 1\n    },\n    {\n      "name": "strong",\n      "vibrationScale": 1.3,\n      "suctionScale": 1.1,\n      "contactSpan": 0.55,\n      "contactCurve": "peak"\n    }\n  ],\n  "activeStrength": "normal",\n  "contactMarks": {\n    "tip": {\n      "x": 1,\n      "y": 2,\n      "w": 3,\n      "h": 4,\n      "class": "c"\n    },\n    "extras": [\n      {\n        "x": 1,\n        "y": 0,\n        "w": 0,\n        "h": 0\n      }\n    ],\n    "drive_stroke": false\n  },\n  "trajectory": {\n    "width": 10,\n    "height": 20,\n    "tip": [\n      {\n        "atMs": 1,\n        "x": 1.5,\n        "y": 2\n      }\n    ]\n  },\n  "sceneMap": {\n    "version": 1,\n    "video": {\n      "durationMs": 0,\n      "width": 0,\n      "height": 0\n    },\n    "grid": {\n      "cols": 0,\n      "rows": 0\n    },\n    "windows": [\n      {\n        "startMs": 0,\n        "endMs": 5,\n        "tempoHz": 1.25,\n        "score_b64": "AAE=",\n        "chosen": -1,\n        "box": [\n          1,\n          2.5\n        ]\n      }\n    ]\n  },\n  "qualityScore": 0.5,\n  "qualityPassed": true,\n  "qualityWarnings": [\n    "w"\n  ]\n}'
GO_GOLDEN_EXPORT = '{\n  "actions": [\n    {\n      "at": 0,\n      "pos": 20\n    },\n    {\n      "at": 1000,\n      "pos": 100\n    }\n  ],\n  "metadata": {\n    "ai_opinion": {\n      "verdict": "ok",\n      "reason": "r"\n    },\n    "bookmarks": [\n      {\n        "name": "B",\n        "time": 500\n      }\n    ],\n    "chapters": [\n      {\n        "name": "A",\n        "startTime": 0,\n        "endTime": 5\n      }\n    ],\n    "contact_marks": {\n      "tip": {\n        "x": 1,\n        "y": 2,\n        "w": 3,\n        "h": 4,\n        "class": "c"\n      },\n      "extras": [\n        {\n          "x": 1,\n          "y": 0,\n          "w": 0,\n          "h": 0\n        }\n      ],\n      "drive_stroke": false\n    },\n    "creator": "te\\u003cs\\u003et \\u0026 ü",\n    "device_recipe": {\n      "sync": "suction_position",\n      "min_suction": 0.2,\n      "tick_ms": 50,\n      "max_speed": 0.5,\n      "smoothing": 0.22,\n      "contact_vibration": true,\n      "contact_vibration_span": 0.5,\n      "contact_vibration_curve": "soft",\n      "feel_balance": 1e-7,\n      "scene_ranges": [\n        {\n          "start_ms": 0,\n          "end_ms": 10,\n          "feel": "oral"\n        }\n      ]\n    },\n    "duration": 1000,\n    "oMarkers": [\n      {\n        "startMs": 1,\n        "endMs": 2,\n        "kind": "primary",\n        "intensity": 1\n      }\n    ],\n    "profile": "tj",\n    "quality_passed": true,\n    "quality_score": 0.5,\n    "quality_warnings": [\n      "w"\n    ],\n    "tags": [\n      "a"\n    ],\n    "title": "T",\n    "tracking_gaps": [\n      {\n        "start_ms": 1,\n        "end_ms": 2\n      }\n    ],\n    "trajectory": {\n      "width": 10,\n      "height": 20,\n      "tip": [\n        {\n          "atMs": 1,\n          "x": 1.5,\n          "y": 2\n        }\n      ]\n    }\n  },\n  "range": 90.0,\n  "version": "1.0",\n  "z": {\n    "b": [\n      1.50,\n      2e3\n    ],\n    "a": "\\u003cx\\u003e"\n  }\n}'
_GOLDEN_OLD_TARGET = (
    '{"actions":[{"at":0,"pos":0}],"inverted":true,"range":90.0,"version":"1.0",'
    '"z":{"b":[1.50,2e3],"a":"<x>"},"metadata":{"title":"T","duration":1.0,"tags":["a"],'
    '"ai_opinion":{"verdict":"ok","reason":"r"},"samn_axes":{"vibration":[]}}}')


# --- convert_test.go -------------------------------------------------------

def test_round_trip_funscript_samn(tmp_path):  # TestRoundTripFunscriptSamn
    fs_path = tmp_path / "clip.funscript"
    body = """{
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
}"""
    fs_path.write_text(body, encoding="utf-8")

    d = sf.load_funscript_file(fs_path)
    assert len(d.general) == 3
    assert len(d.chapters) == 1 and len(d.bookmarks) == 1
    d.o_markers = [OMarker(start_ms=800, end_ms=1200, kind=sf.O_MARKER_PRIMARY, intensity=1)]
    d.bake_neo_axes()
    assert d.playback_source == sf.PLAYBACK_AXES
    assert len(d.vibration) >= 2 and len(d.suction) >= 2
    d.strength_presets = sf.default_strength_presets()
    d.active_strength = "normal"

    samn_path = tmp_path / "clip.samn"
    sf.save(samn_path, d)
    got = sf.load(samn_path)
    assert got.active_strength == "normal" and len(got.strength_presets) == 3

    # Player bridge
    script = got.to_funscript()
    opts = sf.map_options_from_script(script)
    assert opts.use_explicit_axes, "expected axes playback from .samn"
    frames = to_intensity_curve(script.actions, opts, script.inverted)
    assert frames

    # Export must not embed samn_axes
    out_fs = tmp_path / "out.funscript"
    got.export_funscript(out_fs)
    raw = out_fs.read_text(encoding="utf-8")
    assert "samn_axes" not in raw
    reloaded = sf.load_funscript(out_fs)
    assert len(reloaded.actions) == 3
    oms = sf.load_o_markers(out_fs)
    assert len(oms) == 1


def test_is_samn_path():  # TestIsSamnPath
    assert sf.is_samn_path("a.samn") and not sf.is_samn_path("a.funscript")
    assert sf.companion_samn_path("x.funscript") == "x.samn"
    # same rules as Go filepath.Ext
    assert sf.is_samn_path("A.SAMN")
    assert sf.companion_samn_path("dir.d/noext") == "dir.d/noext.samn"
    assert sf.companion_funscript_path("/a/b.samn") == "/a/b.funscript"


# --- export_merge_test.go --------------------------------------------------

def test_export_funscript_keeps_foreign_metadata(tmp_path):  # TestExportFunscriptKeepsForeignMetadata
    p = tmp_path / "x.funscript"
    orig = """{"actions":[{"at":0,"pos":0},{"at":500,"pos":100}],"inverted":false,"range":90,"version":"1.0",
	 "metadata":{"title":"My scene","tags":["a"],"performers":["p"],"script_url":"u","duration":1.0,
	  "audio_check":{"script_hz":1.5,"audio_hz":1.4,"warnings":[],"speech_hold_ms":500,
	   "segments":[{"label":"gentle","start_ms":0,"end_ms":400}]},
	  "ai_opinion":{"verdict":"ok","reason":"r"},
	  "tracking_gaps":[{"start_ms":100,"end_ms":200}],
	  "samn_axes":{"vibration":[{"at":0,"pos":10}]}}}"""
    p.write_text(orig, encoding="utf-8")
    d = Document(general=[(0, 5), (1000, 95)])
    d.export_funscript(p)
    top = json.loads(p.read_text(encoding="utf-8"))
    for k in ("range", "version"):
        assert k in top, f"top-level {k!r} lost"
    meta = top["metadata"]
    for k in ("title", "tags", "performers", "script_url", "audio_check", "ai_opinion"):
        assert k in meta, f"metadata {k!r} lost"
    # What the export manages follows the document - also by omission.
    for k in ("tracking_gaps", "samn_axes"):
        assert k not in meta, f"metadata {k!r} must follow the document (dropped)"
    # Exported points are already as played: a kept "inverted" would flip twice.
    assert "inverted" not in top, 'top-level "inverted" must not survive the export'
    s = sf.load_funscript(p)
    assert len(s.actions) == 2 and s.actions[1] == (1000, 95), f"actions not replaced: {s.actions}"
    ac = meta["audio_check"]
    assert len(ac["segments"]) == 1 and s.metadata.duration == 1000, f"audio_check {ac} duration"


def test_export_funscript_without_usable_target(tmp_path):  # TestExportFunscriptWithoutUsableTarget
    d = Document(general=[(0, 5), (1000, 95)])
    for name in ("new.funscript", "broken.funscript"):
        p = tmp_path / name
        if name == "broken.funscript":
            p.write_text("{not json", encoding="utf-8")
        d.export_funscript(p)
        sf.load_funscript(p)
        assert not os.path.exists(str(p) + ".tmp"), f"{name}: temp file left behind"
    assert sorted(os.listdir(tmp_path)) == ["broken.funscript", "new.funscript"]


def test_from_funscript_bakes_inverted():  # TestFromFunscriptBakesInverted
    s = sf.parse_funscript('{"actions":[{"at":0,"pos":10},{"at":1000,"pos":80}],"inverted":true}')
    d = sf.from_funscript(s, "")
    assert d.general[0][1] == 90 and d.general[1][1] == 20, \
        f"general {d.general}, want played positions 90/20"


# --- additions: round trip, Go byte compatibility, rules ------------------

def _golden_document() -> Document:
    return Document(
        creator="te<s>t & ü", video_path="/v/a.mp4", profile="tf",
        recipe=sf.DeviceRecipe(
            sync="suction_position", min_suction=0.2, tick_ms=50, max_speed=0.5,
            smoothing=0.22, contact_vibration=True, contact_vibration_span=0.5,
            contact_vibration_curve="soft", feel_balance=1e-7,
            scene_ranges=[SceneFeelRange(0, 10, "oral")]),
        general=[(1000, 150), (0, 20)], vibration=[(0, 1)],
        chapters=[sf.ChapterMark("A", 0, 5)], bookmarks=[sf.Bookmark("B", 500)],
        o_markers=[OMarker(1, 2, "primary", 1.0)], tracking_gaps=[TrackingGap(1, 2)],
        strength_presets=sf.default_strength_presets(), active_strength="normal",
        contact_marks=sf.ContactMarks(tip=sf.ContactMarkBox(1, 2, 3, 4, "c"),
                                      extras=[sf.ContactMarkBox(x=1)]),
        trajectory=sf.TrajectoryData(10, 20, [sf.TrajectoryPoint(1, 1.5, 2.0)]),
        scene_map=sf.SceneMapData(version=1, windows=[
            sf.SceneMapWindow(0, 5, 1.25, "AAE=", -1, box=[1.0, 2.5])]),
        quality_score=0.5, quality_passed=True, quality_warnings=["w"])


def test_python_write_is_byte_identical_to_go(tmp_path):
    p = tmp_path / "g.samn"
    sf.save(p, _golden_document())
    assert p.read_bytes().decode("utf-8") == GO_GOLDEN_SAMN


def test_export_is_byte_identical_to_go(tmp_path):
    p = tmp_path / "t.funscript"
    p.write_text(_GOLDEN_OLD_TARGET, encoding="utf-8")
    _golden_document().export_funscript(p)
    assert p.read_bytes().decode("utf-8") == GO_GOLDEN_EXPORT


def test_go_file_reads_and_rewrites_identically(tmp_path):
    d = sf.parse(GO_GOLDEN_SAMN)
    assert d.general == [(0, 20), (1000, 100)]  # sorted + clamped by Normalize
    assert d.profile == "tj" and d.playback_source == "recipe" and d.duration_ms == 1000
    assert d.scene_map.windows[0].chosen == -1
    assert sf.dumps(d).decode("utf-8") == GO_GOLDEN_SAMN


def test_write_then_read_round_trip(tmp_path):
    p = tmp_path / "rt.samn"
    d = _golden_document()
    sf.save(p, d)  # normalizes d in place, like Go
    got = sf.load(p)
    assert got == d
    assert got.general == [(0, 20), (1000, 100)]
    assert all(isinstance(a, tuple) for a in got.general + got.vibration)
    assert os.listdir(tmp_path) == ["rt.samn"]  # no temp file left behind


def test_json_keys_and_order_match_go(tmp_path):
    p = tmp_path / "k.samn"
    sf.save(p, Document(general=[(0, 5), (1000, 95)]))
    top = json.loads(p.read_text(encoding="utf-8"))
    # minimal document: omitempty drops everything but these (Go struct order)
    assert list(top) == ["version", "kind", "durationMs", "profile", "playbackSource",
                         "recipe", "general"]
    assert list(top["recipe"]) == ["sync", "min_suction", "tick_ms", "max_speed", "smoothing"]
    assert top["general"][0] == {"at": 0, "pos": 5}
    assert list(json.loads(GO_GOLDEN_SAMN)) == list(json.loads(
        sf.dumps(sf.parse(GO_GOLDEN_SAMN))))


def test_unknown_fields_are_dropped_like_go():
    d = sf.parse('{"general":[{"at":0,"pos":1.6}],"future":{"x":1},"Creator":"c"}')
    assert d.general == [(0, 2)]  # float pos rounded; keys match case-insensitively
    assert d.creator == "c"
    assert "future" not in sf.dumps(d).decode()


@pytest.mark.parametrize("text,msg", [
    ("{nope", "samn: invalid JSON"),
    ('{"general":[]}', "samn: general curve is required"),
    ('{"version":2,"general":[{"at":0,"pos":1}]}', "samn: unsupported version 2 (want 1)"),
    ('{"kind":"x","general":[{"at":0,"pos":1}]}', 'samn: unexpected kind "x"'),
    ('{"general":[{"at":-1,"pos":1}]}', "negative Zeit"),
    ('{"general":[{"at":0,"pos":1}],"oMarkers":[{"startMs":5,"endMs":5,"kind":"primary"}]}',
     "samn: oMarkers[0]: funscript: O-Marker Ende"),
])
def test_invalid_documents(text, msg):
    with pytest.raises(SamnError, match=re.escape(msg)):
        sf.parse(text)


def test_prefer_samn_companion(tmp_path):  # cmd/gui-wails preferSamnCompanion
    fun = tmp_path / "a.funscript"
    fun.write_text("{}")
    assert sf.prefer_samn_companion(fun) == str(fun)  # no companion yet
    (tmp_path / "a.samn").mkdir()  # a directory does not count
    assert sf.prefer_samn_companion(fun) == str(fun)
    (tmp_path / "a.samn").rmdir()
    (tmp_path / "a.samn").write_text("{}")
    assert sf.prefer_samn_companion(fun) == str(tmp_path / "a.samn")
    assert sf.prefer_samn_companion(tmp_path / "a.samn") == str(tmp_path / "a.samn")
    assert sf.prefer_samn_companion("") == ""
