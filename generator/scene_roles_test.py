#!/usr/bin/env python3
"""Unit tests for scene_roles.py (stage 2): scan decoding, part motion,
NudeNet part mapping, role rules, and the contact-points output the Go
loader reads. No model, no video: parts and scans are built inline."""

import base64
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import scene_roles as sr  # noqa: E402

COLS, ROWS = 16, 9


def heat(cells, value=255):
    """Score grid (0..1) with the given (col, row) cells hot."""
    s = [0.0] * (COLS * ROWS)
    for c, r in cells:
        s[r * COLS + c] = value / 255.0
    return s


def box_cells(c0, c1, r0, r1):
    return [c0 / COLS, r0 / ROWS, (c1 + 1) / COLS, (r1 + 1) / ROWS]


def part(cls, cells_box, source="t"):
    return {"class": cls, "box": box_cells(*cells_box), "score": 0.9, "source": source}


class ScanTest(unittest.TestCase):
    def test_decode_base64_and_keep_longest_per_sample(self):
        raw = bytes([0] * (COLS * ROWS - 1) + [255])
        b64 = base64.b64encode(raw).decode()
        scan = {"cols": COLS, "rows": ROWS, "width": 1280, "height": 720, "windows": [
            {"startMs": 0, "endMs": 5000, "tempoHz": 1, "score": b64},
            {"startMs": 1000, "endMs": 8000, "tempoHz": 1.2, "score": b64},   # longest of sample 1
            {"startMs": 10000, "endMs": 18000, "tempoHz": 1, "score": b64},
            {"startMs": 20000, "endMs": 28000, "tempoHz": 1, "score": "AAAA"},  # wrong size: dropped
        ]}
        cols, rows, w, h, wins = sr.decode_scan(scan)
        self.assertEqual((cols, rows, w, h), (16, 9, 1280, 720))
        self.assertEqual([x["start_ms"] for x in wins], [1000, 10000])
        self.assertEqual(wins[0]["score"][-1], 1.0)
        self.assertEqual(wins[0]["mid_ms"], 4500)

    def test_part_motion(self):
        s = heat([(7, 6), (8, 6)])
        self.assertAlmostEqual(sr.part_motion(box_cells(7, 8, 6, 6), s, COLS, ROWS), 1.0)
        # 4 cells, 2 hot: the strongest third (2 cells) are both hot.
        self.assertAlmostEqual(sr.part_motion(box_cells(7, 8, 5, 6), s, COLS, ROWS), 1.0)
        # 9 cells, 1 hot: strongest third = 3 cells -> 1/3.
        self.assertAlmostEqual(sr.part_motion(box_cells(6, 8, 5, 7), heat([(7, 6)]), COLS, ROWS), 1 / 3)
        tiny = [7.4 / 16, 6.4 / 9, 7.5 / 16, 6.5 / 9]  # smaller than a cell: nearest cell
        self.assertAlmostEqual(sr.part_motion(tiny, s, COLS, ROWS), 1.0)


class NudeNetPartsTest(unittest.TestCase):
    def test_mapping_breast_pair_and_derived_mouth(self):
        d = lambda c, x, y, w, h, s=0.8: {"class": c, "score": s, "box": [x, y, w, h]}
        parts = sr.nudenet_parts([
            d("FEMALE_BREAST_EXPOSED", 500, 400, 100, 100), d("FEMALE_BREAST_EXPOSED", 640, 400, 100, 100),
            d("FACE_FEMALE", 560, 100, 120, 150), d("MALE_GENITALIA_EXPOSED", 600, 500, 40, 100),
            d("FEET_EXPOSED", 0, 0, 10, 10), d("MALE_GENITALIA_EXPOSED", 0, 0, 5, 5, 0.1)], 1280, 720)
        classes = sorted(p["class"] for p in parts)
        self.assertEqual(classes, ["breasts", "face", "mouth", "penis"])
        br = [p for p in parts if p["class"] == "breasts"][0]
        self.assertAlmostEqual(br["box"][0], 500 / 1280)
        self.assertAlmostEqual(br["box"][2], 740 / 1280)
        mouth = [p for p in parts if p["class"] == "mouth"][0]
        self.assertEqual(mouth["source"], "nudenet:derived")
        self.assertGreater(mouth["box"][1], 100 / 720)


class RolesTest(unittest.TestCase):
    def test_moving_mouth_on_penis_is_blowjob_mouth_primary(self):
        parts = [part("penis", (7, 7, 6, 8)), part("mouth", (7, 8, 5, 5)), part("face", (12, 13, 1, 3))]
        r = sr.assign_roles(parts, heat([(7, 5), (8, 5)]), COLS, ROWS)
        self.assertEqual(r["scene_type"], "blowjob")
        self.assertEqual(r["primary"]["class"], "mouth")
        self.assertEqual(r["partner"]["class"], "penis")
        self.assertGreaterEqual(r["confidence"], 0.5)
        # The other person's face is not near the pair: ignore proposal.
        self.assertEqual([p["class"] for p in r["ignore"]], ["face"])

    def test_moving_penis_between_breasts_is_titjob_penis_primary(self):
        parts = [part("penis", (7, 7, 6, 8)), part("breasts", (5, 9, 5, 7))]
        r = sr.assign_roles(parts, heat([(7, 6), (7, 7), (7, 8)]), COLS, ROWS)
        self.assertEqual((r["scene_type"], r["primary"]["class"], r["partner"]["class"]),
                         ("titjob", "penis", "breasts"))

    def test_nipples_and_glans_type_titjob_keeping_class_ids(self):
        # Contact-vibe / Train use nipples + glans; before, neither was in
        # PARTNERS / tip set so the window stayed untyped.
        parts = [part("glans", (7, 7, 6, 8)), part("nipples", (5, 9, 5, 7))]
        r = sr.assign_roles(parts, heat([(7, 6), (7, 7), (7, 8)]), COLS, ROWS)
        self.assertEqual((r["scene_type"], r["primary"]["class"], r["partner"]["class"]),
                         ("titjob", "glans", "nipples"))

    def test_hand_1_alias_types_handjob(self):
        parts = [part("penis", (7, 7, 6, 8)), part("hand_1", (6, 8, 5, 7))]
        r = sr.assign_roles(parts, heat([(7, 6), (7, 7)]), COLS, ROWS)
        self.assertEqual(r["scene_type"], "handjob")
        self.assertEqual({r["primary"]["class"], r["partner"]["class"]}, {"penis", "hand_1"})

    def test_hidden_penis_uses_moving_part_with_low_confidence(self):
        parts = [part("breasts", (6, 8, 5, 7)), part("face", (6, 8, 1, 3))]
        r = sr.assign_roles(parts, heat([(7, 6), (7, 7)]), COLS, ROWS)
        self.assertEqual(r["scene_type"], "titjob")
        self.assertIsNone(r["partner"])
        self.assertLessEqual(r["confidence"], 0.6)

    def test_far_apart_pair_and_no_motion(self):
        far = [part("penis", (1, 1, 7, 8)), part("mouth", (13, 14, 1, 1))]
        r = sr.assign_roles(far, heat([(13, 1), (14, 1)]), COLS, ROWS)
        self.assertIsNone(r["partner"])  # not a pair: only the moving part
        still = [part("penis", (7, 7, 6, 8)), part("mouth", (7, 8, 5, 5))]
        r = sr.assign_roles(still, heat([]), COLS, ROWS)
        self.assertIsNone(r["scene_type"])
        self.assertIsNone(r["primary"])


class BuildTest(unittest.TestCase):
    def test_build_proposals_and_contact_points(self):
        b64 = base64.b64encode(bytes(int(v * 255) for v in heat([(7, 5), (8, 5)]))).decode()
        scan = {"cols": COLS, "rows": ROWS, "width": 1280, "height": 720, "windows": [
            {"startMs": 0, "endMs": 8000, "tempoHz": 1.1, "score": b64},
            {"startMs": 10000, "endMs": 18000, "tempoHz": 1.1, "score": b64}]}

        def parts_at(t):
            if t < 9000:
                return [part("penis", (7, 7, 6, 8)), part("mouth", (7, 8, 5, 5))]
            return []
        res = sr.build("clip.mp4", scan, parts_at, log=lambda *_: None)
        self.assertEqual(res["summary"]["scene_types"], {"blowjob": 1})
        prop = res["proposals"][0]
        self.assertEqual(prop["primary"]["class"], "mouth")
        self.assertEqual((prop["start_ms"], prop["end_ms"]), (0, 8000))
        self.assertEqual((prop["primary"]["x"], prop["primary"]["y"],
                          prop["primary"]["w"], prop["primary"]["h"]), (560, 400, 160, 80))
        self.assertEqual(prop["partner"]["class"], "penis")
        cp = sr.contact_points_from_scene(res)
        self.assertEqual(cp["version"], 1)
        ts = [p["t_ms"] for p in cp["points"]]
        self.assertEqual(ts, list(range(0, 8001, 500)))
        for p in cp["points"]:  # what LoadContactPoints (Go) requires
            self.assertTrue(0 <= p["x"] <= 1 and 0 <= p["y"] <= 1)
            self.assertEqual(p["agree"], 1)
        self.assertAlmostEqual(cp["points"][0]["x"], 0.5)
        # Below the confidence floor a window gives no points.
        self.assertEqual(sr.contact_points_from_scene(res, min_confidence=1.01)["points"], [])


class VlmClipTest(unittest.TestCase):
    """Stage 2b: the VLM's own scene reading is recorded and scored, not applied."""

    def _res(self):
        b64 = base64.b64encode(bytes(int(v * 255) for v in heat([(7, 5), (8, 5)]))).decode()
        scan = {"cols": COLS, "rows": ROWS, "width": 1280, "height": 720, "windows": [
            {"startMs": 0, "endMs": 8000, "tempoHz": 1.1, "score": b64},
            {"startMs": 10000, "endMs": 18000, "tempoHz": 1.1, "score": b64},
            {"startMs": 40000, "endMs": 48000, "tempoHz": 1.1, "score": b64}]}
        vlm = {"clip": 6, "frames": [
            {"t_ms": 4500, "clip": {"scene_type": "blowjob", "moving": "mouth"}},
            {"t_ms": 13000, "clip": {"scene_type": "titjob", "moving": "penis"}},
            {"t_ms": 30000, "clip": None}]}
        parts = lambda t: [part("penis", (7, 7, 6, 8)), part("mouth", (7, 8, 5, 5))]
        return sr.build("clip.mp4", scan, parts, log=lambda *_: None, clip_at=sr.clip_at_fn(vlm))

    def test_recorded_per_window_and_agreement_counted(self):
        res = self._res()
        self.assertEqual([(w["vlm"] or {}).get("scene_type") for w in res["windows"]],
                         ["blowjob", "titjob", None])  # 44 s: nearest answer 13 s is too far
        self.assertEqual(res["windows"][0]["vlm"]["t_ms"], 4500)
        self.assertEqual(res["summary"]["vlm"], {"windows": 2, "both": 2, "agree": 1})
        # Roles stay the rules' (the VLM reading is not applied yet).
        self.assertEqual([w["scene_type"] for w in res["windows"]], ["blowjob"] * 3)

    def test_score_against_truth(self):
        truth = {"scene_types": [{"t_ms": 4000, "scene_type": "blowjob"},
                                 {"t_ms": 14000, "scene_type": "titjob"},
                                 {"t_ms": 44000, "scene_type": "blowjob"},
                                 {"t_ms": 90000, "scene_type": "handjob"}]}  # no window near
        self.assertEqual(sr.score_scene_types(self._res(), truth),
                         {"labelled": 3, "rules": {"typed": 3, "right": 2},
                          "vlm": {"typed": 2, "right": 2}})

    def test_truth_file_in_repo_is_valid(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "testdata", "vlm_labels", "multi_person_642s.scene_types.json")
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        self.assertEqual(len(doc["scene_types"]), 27)
        self.assertTrue({x["scene_type"] for x in doc["scene_types"]} <= set(sr.PARTNERS.values()))


if __name__ == "__main__":
    unittest.main()
