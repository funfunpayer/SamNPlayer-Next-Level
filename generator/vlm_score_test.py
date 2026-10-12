#!/usr/bin/env python3
"""Unit tests for vlm_score (V0 gate scorer) and the committed golden
reference labels."""

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import vlm_score as vs  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLES = [
    os.path.join(HERE, "testdata", "golden_clips", "clip_voll_tftj", "vlm_oracle.json"),
    os.path.join(HERE, "testdata", "golden_clips", "clip_ausschnitt_native", "vlm_oracle.json"),
    os.path.join(HERE, "testdata", "vlm_labels", "multi_person_642s.json"),
]

ORACLE = {"keyframes": [
    {"t_ms": 5000, "contact": [0.4, 0.6, 0.6, 1.0], "exclude": [[0.8, 0.5, 1.0, 1.0]]},
    {"t_ms": 15000, "contact": [0.4, 0.6, 0.6, 1.0], "exclude": [[0.8, 0.5, 1.0, 1.0]]},
    {"t_ms": 25000, "contact": [0.4, 0.6, 0.6, 1.0], "exclude": []},
    {"t_ms": 35000, "contact": [0.4, 0.6, 0.6, 1.0], "exclude": []},
]}


def box(label, x0, y0, x1, y1):
    return {"label": label, "x0": x0, "y0": y0, "x1": x1, "y1": y1}


class ScoreTest(unittest.TestCase):
    def test_hit_miss_exclude_and_missing(self):
        probe = {"frames": [
            {"t_ms": 5500, "status": "ok", "boxes": [box("contact", 0.45, 0.65, 0.55, 0.95),
                                                     box("thigh", 0.8, 0.5, 1.0, 1.0)]},
            {"t_ms": 14000, "status": "ok", "boxes": [box("contact", 0.82, 0.6, 0.98, 0.9)]},
            {"t_ms": 25000, "status": "refused", "boxes": []},
            # 35 s has no probe frame within the gap.
            {"t_ms": 40000, "status": "ok", "boxes": [box("contact", 0.4, 0.6, 0.6, 1.0)]},
        ]}
        res = vs.score(probe, ORACLE)
        self.assertEqual(res["keyframes"], 4)
        self.assertEqual(res["matched"], 3)
        self.assertAlmostEqual(res["contact_rate"], 2 / 3, places=3)
        self.assertAlmostEqual(res["hit_rate"], 1 / 3, places=3)
        self.assertAlmostEqual(res["on_exclude_rate"], 1 / 3, places=3)
        r0 = res["rows"][0]
        self.assertTrue(r0["hit"])
        self.assertAlmostEqual(r0["iou"], (0.1 * 0.3) / (0.2 * 0.4), places=3)
        self.assertFalse(res["rows"][3]["matched"])

    def test_largest_contact_box_counts(self):
        probe = {"frames": [{"t_ms": 5000, "status": "ok", "boxes": [
            box("contact", 0.0, 0.0, 0.05, 0.05),
            box("contact", 0.42, 0.62, 0.58, 0.98)]}]}
        res = vs.score(probe, {"keyframes": ORACLE["keyframes"][:1]})
        self.assertEqual(res["hit_rate"], 1.0)

    def test_empty_probe(self):
        res = vs.score({"frames": []}, ORACLE)
        self.assertEqual(res["matched"], 0)
        self.assertIsNone(res["hit_rate"])

    def test_exemplar_keyframes_are_not_scored(self):
        probe = {"exemplar_t_ms": [5000], "frames": [
            {"t_ms": 15000, "status": "ok", "boxes": [box("contact", 0.45, 0.65, 0.55, 0.95)]}]}
        res = vs.score(probe, {"keyframes": ORACLE["keyframes"][:2]})
        self.assertEqual(res["skipped_exemplars"], 1)
        self.assertEqual(res["keyframes"], 1)
        self.assertEqual(res["hit_rate"], 1.0)

    def test_iou(self):
        self.assertEqual(vs.iou([0, 0, 1, 1], [0, 0, 1, 1]), 1.0)
        self.assertEqual(vs.iou([0, 0, 0.5, 0.5], [0.5, 0.5, 1, 1]), 0.0)


class GoldenOracleTest(unittest.TestCase):
    def test_committed_labels_are_well_formed(self):
        for path in ORACLES:
            with open(path, encoding="utf-8") as f:
                doc = json.load(f)
            kfs = doc["keyframes"]
            self.assertGreaterEqual(len(kfs), 10, path)
            ts = [k["t_ms"] for k in kfs]
            self.assertEqual(ts, sorted(ts), path)
            for k in kfs:
                for b in [k["contact"]] + k["exclude"]:
                    self.assertEqual(len(b), 4)
                    self.assertTrue(0 <= b[0] < b[2] <= 1 and 0 <= b[1] < b[3] <= 1, (path, k))
                # A contact box never sits inside a thigh/hand exclude.
                c = ((k["contact"][0] + k["contact"][2]) / 2, (k["contact"][1] + k["contact"][3]) / 2)
                self.assertFalse(any(e[0] <= c[0] <= e[2] and e[1] <= c[1] <= e[3]
                                     for e in k["exclude"]), (path, k["t_ms"]))

    def test_oracle_scores_itself_perfectly(self):
        with open(ORACLES[0], encoding="utf-8") as f:
            doc = json.load(f)
        probe = {"frames": [{"t_ms": k["t_ms"], "status": "ok",
                             "boxes": [box("contact", *k["contact"])]} for k in doc["keyframes"]]}
        res = vs.score(probe, doc)
        self.assertEqual(res["hit_rate"], 1.0)
        self.assertEqual(res["mean_iou"], 1.0)
        self.assertEqual(res["on_exclude_rate"], 0.0)


if __name__ == "__main__":
    unittest.main()
