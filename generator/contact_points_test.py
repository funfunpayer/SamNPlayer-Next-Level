#!/usr/bin/env python3
"""Unit tests for contact_points.py - no NudeNet, no model: a fake
detector stands in, VLM probe results are built inline."""

import json
import os
import sys
import tempfile
import unittest

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import contact_points as cp  # noqa: E402


def det(cls, score, x, y, w, h):
    return {"class": cls, "score": score, "box": [x, y, w, h]}


class RuleTest(unittest.TestCase):
    W, H = 1280, 720

    def test_penis_wins(self):
        c = cp.contact_from_parts([det("FEMALE_BREAST_EXPOSED", 0.9, 500, 400, 100, 100),
                                   det("FEMALE_BREAST_EXPOSED", 0.9, 640, 400, 100, 100),
                                   det("MALE_GENITALIA_EXPOSED", 0.6, 600, 500, 40, 100)],
                                  self.W, self.H)
        self.assertEqual(c[2], "penis")
        self.assertAlmostEqual(c[0], 620 / 1280)
        self.assertAlmostEqual(c[1], 550 / 720)
        self.assertEqual(len(c[4]), 4)

    def test_cleavage_between_pair(self):
        c = cp.contact_from_parts([det("FEMALE_BREAST_EXPOSED", 0.8, 500, 400, 100, 100),
                                   det("FEMALE_BREAST_EXPOSED", 0.7, 640, 410, 100, 100),
                                   det("FACE_FEMALE", 0.9, 560, 100, 120, 140)],
                                  self.W, self.H)
        self.assertEqual(c[2], "cleavage")
        self.assertAlmostEqual(c[0], (550 + 690) / 2 / 1280)
        self.assertAlmostEqual(c[1], (410 + 60) / 720)
        self.assertAlmostEqual(c[3], 0.7)

    def test_two_peoples_breasts_are_not_a_pair(self):
        far = cp.contact_from_parts([det("FEMALE_BREAST_EXPOSED", 0.8, 100, 400, 80, 80),
                                     det("FEMALE_BREAST_EXPOSED", 0.8, 900, 400, 80, 80)],
                                    self.W, self.H)
        self.assertIsNone(far)

    def test_no_face_rule_and_low_scores_ignored(self):
        self.assertIsNone(cp.contact_from_parts([det("FACE_FEMALE", 0.95, 560, 100, 120, 140)],
                                                self.W, self.H))
        self.assertIsNone(cp.contact_from_parts([det("MALE_GENITALIA_EXPOSED", 0.1, 1, 1, 9, 9)],
                                                self.W, self.H))


class AgreementTest(unittest.TestCase):
    def test_distinct_teachers_close_in_space_and_time(self):
        pts = [
            {"t_ms": 1000, "x": 0.50, "y": 0.70, "source": "nudenet"},
            {"t_ms": 1400, "x": 0.52, "y": 0.72, "source": "vlm:qwen"},   # agrees
            {"t_ms": 1500, "x": 0.51, "y": 0.70, "source": "nudenet"},   # same teacher
            {"t_ms": 9000, "x": 0.50, "y": 0.70, "source": "vlm:qwen"},   # too late
            {"t_ms": 1200, "x": 0.90, "y": 0.10, "source": "vlm:intern"},  # too far
        ]
        cp.count_agreement(pts)
        self.assertEqual([p["agree"] for p in pts], [2, 2, 2, 1, 1])


class BuildTest(unittest.TestCase):
    def _video(self, d):
        path = os.path.join(d, "clip.avi")
        vw = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"MJPG"), 10, (320, 180))
        for i in range(40):
            vw.write(np.full((180, 320, 3), 60, np.uint8))
        vw.release()
        return path

    def test_nudenet_plus_vlm_consensus_and_go_format(self):
        calls = []

        def fake_detect(frame):
            calls.append(frame.shape)
            if len(calls) % 2:
                return [det("MALE_GENITALIA_EXPOSED", 0.7, 150, 100, 20, 40)]
            return []
        with tempfile.TemporaryDirectory() as d:
            video = self._video(d)
            vlm = os.path.join(d, "clip.vlm.json")
            with open(vlm, "w") as f:
                json.dump({"model": "qwen2.5vl:7b", "frames": [
                    {"t_ms": 0, "boxes": [{"label": "contact", "x0": 0.45, "y0": 0.6,
                                            "x1": 0.55, "y1": 0.8}]},
                    {"t_ms": 2000, "boxes": [{"label": "thigh", "x0": 0, "y0": 0, "x1": 1, "y1": 1}]}]}, f)
            res = cp.build(video, use_nudenet=True, vlm_files=[vlm], step_s=0.5,
                           detect=fake_detect, log=lambda *_: None)
            out = os.path.join(d, "clip.contact.json")
            self.assertEqual(cp.main(["--video", video, "--vlm", vlm, "--out", out]), 0)
            with open(out) as f:
                vlm_only = json.load(f)
        self.assertEqual(len(calls), 8)  # 40 frames / (0.5 s * 10 fps)
        self.assertEqual(res["version"], 1)
        self.assertEqual(res["teachers"], ["nudenet", "vlm:qwen2.5vl:7b"])
        nn = [p for p in res["points"] if p["source"] == "nudenet"]
        self.assertEqual([p["t_ms"] for p in nn], [0, 1000, 2000, 3000])
        self.assertEqual(nn[0]["rule"], "penis")
        # t=0: NudeNet (0.5, 0.667) and the VLM box centre (0.5, 0.7) agree.
        self.assertEqual(nn[0]["agree"], 2)
        self.assertEqual(nn[1]["agree"], 2)  # 1000 ms: still within --agree-ms of it
        self.assertEqual(nn[2]["agree"], 1)
        self.assertEqual(res["summary"]["agree_2plus"], 3)
        for p in res["points"]:  # what LoadContactPoints (Go) requires
            self.assertTrue(0 <= p["x"] <= 1 and 0 <= p["y"] <= 1 and p["t_ms"] >= 0)
            self.assertIsInstance(p["t_ms"], int)
        self.assertEqual(vlm_only["summary"]["points"], 1)

    def test_no_teacher_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(cp.main(["--video", self._video(d)]), 2)


class TeacherRunTest(unittest.TestCase):
    def test_parse_teacher(self):
        self.assertEqual(cp.parse_teacher("ollama:qwen2.5vl:7b"), ("ollama", "qwen2.5vl:7b"))
        with self.assertRaises(ValueError):
            cp.parse_teacher("qwen")

    def test_runs_each_backend_caches_and_skips_failures(self):
        import vlm_probe_test as vpt
        good = vpt.fake_model("norm1000")
        calls = []

        def post(payload):
            calls.append(payload["model"])
            if payload["model"] == "broken":
                raise RuntimeError("server gone")
            return good(payload)
        with tempfile.TemporaryDirectory() as d:
            video = os.path.join(d, "clip.avi")
            vw = cv2.VideoWriter(video, cv2.VideoWriter_fourcc(*"MJPG"), 10, (320, 180))
            for _ in range(60):
                vw.write(np.full((180, 320, 3), 80, np.uint8))
            vw.release()
            specs = ["ollama:qwen2.5vl:7b", "colibri:glm-5.3-flash", "lmstudio:broken"]
            paths = cp.run_vlm_teachers(video, specs, every_s=2.0, post_fn=post, log=lambda *_: None)
            self.assertEqual([os.path.basename(p) for p in paths],
                             ["clip.ollama_qwen2.5vl_7b.vlm.json", "clip.colibri_glm-5.3-flash.vlm.json"])
            with open(paths[1]) as f:
                self.assertEqual(json.load(f)["backend"], "colibri")
            n = len(calls)
            again = cp.run_vlm_teachers(video, specs[:2], post_fn=post, log=lambda *_: None)
            self.assertEqual(again, paths)
            self.assertEqual(len(calls), n)  # cached: no new requests
            res = cp.build(video, vlm_files=paths, log=lambda *_: None)
        self.assertEqual(res["teachers"], ["vlm:qwen2.5vl:7b", "vlm:glm-5.3-flash"])
        # Both fake teachers see the same contact box: every point has a partner.
        self.assertTrue(all(p["agree"] == 2 for p in res["points"]))


if __name__ == "__main__":
    unittest.main()
