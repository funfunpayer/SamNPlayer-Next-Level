#!/usr/bin/env python3
"""Unit tests for vlm_probe (V0 of docs/VLM_TEACHER_PLAN.md) - no model, no
network: a fake chat endpoint answers in each coordinate convention."""

import base64
import json
import os
import sys
import tempfile
import unittest

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import vlm_probe as vp  # noqa: E402

CONTACT = (0.40, 0.30, 0.60, 0.70)  # normalised truth the fake model "sees"


def _image_size(payload):
    url = payload["messages"][0]["content"][1]["image_url"]["url"]
    raw = base64.b64decode(url.split(",", 1)[1])
    img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
    return img.shape[1], img.shape[0]


def fake_model(mode, fence=True):
    """post_fn answering like a model with the given coordinate mode."""
    def to_model(box, w, h):
        x0, y0, x1, y1 = box
        if mode == "pixel":
            return [round(x0 * w), round(y0 * h), round(x1 * w), round(y1 * h)]
        if mode == "norm1000":
            return [round(v * 1000) for v in box]
        return list(box)

    def post(payload):
        w, h = _image_size(payload)
        prompt = payload["messages"][0]["content"][0]["text"]
        if prompt == vp.CALIB_PROMPT:
            items = [{"label": k, "bbox_2d": to_model(v, w, h)}
                     for k, v in vp.CALIB_BOXES.items()]
        else:
            items = [{"label": "contact", "bbox_2d": to_model(CONTACT, w, h)},
                     {"label": "Thighs", "bbox_2d": to_model((0.0, 0.6, 0.3, 1.0), w, h)}]
        text = json.dumps(items)
        if fence:
            text = "Here you go:\n```json\n" + text + "\n```"
        return {"choices": [{"message": {"content": text}}]}
    return post


class SendSizeTest(unittest.TestCase):
    def test_full_hd_to_896(self):
        self.assertEqual(vp.send_size(1920, 1080), (896, 504))

    def test_multiples_of_28_never_upscaled(self):
        for w, h in ((640, 360), (1280, 720), (720, 1280), (300, 200)):
            sw, sh = vp.send_size(w, h)
            self.assertEqual(sw % 28, 0)
            self.assertEqual(sh % 28, 0)
            self.assertLessEqual(sw, max(w, 28))
            self.assertLessEqual(sh, max(h, 28))
            self.assertLessEqual(max(sw, sh), vp.MAX_SIDE)


class ParseAnswerTest(unittest.TestCase):
    def test_fenced_pixel_answer(self):
        text = '```json\n[{"label": "contact", "bbox_2d": [448, 126, 672, 378]}]\n```'
        status, boxes = vp.parse_answer(text, 1 / 896, 1 / 504)
        self.assertEqual(status, "ok")
        b = boxes[0]
        self.assertEqual(b["label"], "contact")
        self.assertAlmostEqual(b["x0"], 0.5, places=3)
        self.assertAlmostEqual(b["y1"], 0.75, places=3)

    def test_dict_wrapper_swapped_corners_and_clip(self):
        text = 'Sure. {"objects": [{"name": "Brust", "box": [1100, 900, 500, -20]}]}'
        status, boxes = vp.parse_answer(text, 1 / 1000, 1 / 1000)
        self.assertEqual(status, "ok")
        b = boxes[0]
        self.assertEqual(b["label"], "breasts")
        self.assertEqual((b["x0"], b["y0"], b["x1"], b["y1"]), (0.5, 0.0, 1.0, 0.9))

    def test_bad_items_dropped(self):
        text = json.dumps([{"label": "hand", "bbox_2d": [1, 2, 3]},
                           {"label": "hand", "bbox_2d": ["a", 0, 1, 1]},
                           {"label": "hand", "bbox_2d": [0.2, 0.2, 0.2, 0.5]},
                           "junk"])
        self.assertEqual(vp.parse_answer(text, 1, 1), ("empty", []))

    def test_empty_refused_unparsed(self):
        self.assertEqual(vp.parse_answer("[]", 1, 1), ("empty", []))
        self.assertEqual(vp.parse_answer("I'm sorry, I can't help with this image.", 1, 1)[0],
                         "refused")
        self.assertEqual(vp.parse_answer("The image shows two people.", 1, 1)[0], "unparsed")
        self.assertEqual(vp.parse_answer(None, 1, 1)[0], "unparsed")

    def test_labels(self):
        cases = {"contact": "contact", "Penetration": "contact", "left hand": "hand",
                 "hand_2": "hand", "Thighs": "thigh", "leg": "thigh", "eichel": "glans",
                 "kopf": "face", "nipples": "breasts", "sofa": "other", "": "other"}
        for raw, want in cases.items():
            self.assertEqual(vp.normalize_label(raw), want, raw)


class CalibrationTest(unittest.TestCase):
    def test_each_convention_is_recognised(self):
        for mode in vp.NAMED_COORDS:
            res = vp.calibrate("fake", "http://x", 1, post_fn=fake_model(mode), w=896, h=504)
            self.assertTrue(res["ok"], (mode, res))
            self.assertEqual(res["nearest_mode"], mode)
            want = vp.coord_scale(mode, 896, 504)
            self.assertAlmostEqual(res["sx"], want[0], delta=want[0] * 0.02)
            self.assertAlmostEqual(res["sy"], want[1], delta=want[1] * 0.02)

    def test_unusable_answers_fail_closed(self):
        self.assertFalse(vp.fit_calibration("no idea", 896, 504)["ok"])
        one = json.dumps([{"label": "red", "bbox_2d": [70, 60, 340, 262]}])
        self.assertFalse(vp.fit_calibration(one, 896, 504)["ok"])
        # Both boxes present but inconsistent with any single scale.
        bad = json.dumps([{"label": "red", "bbox_2d": [800, 10, 850, 20]},
                          {"label": "blue", "bbox_2d": [10, 400, 60, 480]}])
        self.assertFalse(vp.fit_calibration(bad, 896, 504)["ok"])


class KeyframeTest(unittest.TestCase):
    def test_interval_plus_scene_midpoints_deduplicated(self):
        t = vp.keyframe_times_ms(20000, 5.0, [(0, 8000), (8000, 20000)])
        self.assertEqual(t, [2500, 4000, 7500, 12500, 14000, 17500])

    def test_no_interval(self):
        self.assertEqual(vp.keyframe_times_ms(10000, 0, [(0, 10000)]), [5000])


class ProbeVideoTest(unittest.TestCase):
    def _video(self, d):
        path = os.path.join(d, "clip.avi")
        vw = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"MJPG"), 10, (320, 180))
        for i in range(60):
            f = np.full((180, 320, 3), 40 + i, np.uint8)
            vw.write(f)
        vw.release()
        return path

    def test_end_to_end_with_auto_calibration(self):
        for mode in vp.NAMED_COORDS:
            with tempfile.TemporaryDirectory() as d:
                video = self._video(d)
                ov = os.path.join(d, "ov")
                res = vp.probe_video(video, "fake", every_s=2.0, scenes=False,
                                     post_fn=fake_model(mode, fence=(mode != "pixel")),
                                     overlay_dir=ov, log=lambda *_: None)
                self.assertEqual(res["calibration"]["nearest_mode"], mode)
                self.assertEqual((res["sent_w"], res["sent_h"]), (308, 168))
                self.assertEqual([f["t_ms"] for f in res["frames"]], [1000, 3000, 5000])
                for f in res["frames"]:
                    self.assertEqual(f["status"], "ok")
                    c = [b for b in f["boxes"] if b["label"] == "contact"][0]
                    for got, want in zip((c["x0"], c["y0"], c["x1"], c["y1"]), CONTACT):
                        self.assertAlmostEqual(got, want, delta=0.01, msg=mode)
                    self.assertIn("thigh", [b["label"] for b in f["boxes"]])
                s = res["summary"]
                self.assertEqual(s["status"], {"ok": 3})
                self.assertEqual(s["contact_rate"], 1.0)
                self.assertEqual(s["refusal_rate"], 0.0)
                self.assertEqual(len(os.listdir(ov)), 3)
                json.dumps(res)  # the output must be serialisable

    def test_failed_calibration_stops_before_frames(self):
        calls = []

        def post(payload):
            calls.append(payload)
            return {"choices": [{"message": {"content": "sorry, I can't"}}]}
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(RuntimeError):
                vp.probe_video(self._video(d), "fake", scenes=False, post_fn=post,
                               log=lambda *_: None)
        self.assertEqual(len(calls), 1)

    def test_errors_and_refusals_are_recorded_not_fatal(self):
        answers = iter(["sorry, this is explicit content", RuntimeError("timeout"), "[]"])

        def post(payload):
            a = next(answers)
            if isinstance(a, Exception):
                raise a
            return {"choices": [{"message": {"content": a}}]}
        with tempfile.TemporaryDirectory() as d:
            res = vp.probe_video(self._video(d), "fake", every_s=2.0, scenes=False,
                                 coord="norm1", post_fn=post, log=lambda *_: None)
        self.assertEqual([f["status"] for f in res["frames"]], ["refused", "error", "empty"])
        self.assertAlmostEqual(res["summary"]["refusal_rate"], 0.333, places=3)


class ExemplarTest(unittest.TestCase):
    def _video(self, d):
        path = os.path.join(d, "clip.avi")
        vw = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"MJPG"), 10, (320, 180))
        for i in range(60):
            vw.write(np.full((180, 320, 3), 40 + i, np.uint8))
        vw.release()
        return path

    def test_load_exemplars_from_file_and_cli(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "o.json")
            with open(p, "w") as f:
                json.dump({"keyframes": [
                    {"t_ms": 9000, "contact": None, "exclude": []},
                    {"t_ms": 3000, "contact": [0.4, 0.5, 0.6, 0.9], "exclude": [[0, 0, 0.2, 0.2]]},
                    {"t_ms": 5000, "contact": [0.3, 0.5, 0.5, 0.9], "exclude": []},
                    {"t_ms": 7000, "contact": [0.3, 0.5, 0.5, 0.9], "exclude": []}]}, f)
            ex = vp.load_exemplars(p, ["1000:0.1,0.2,0.3,0.4"], count=2)
        self.assertEqual([e["t_ms"] for e in ex], [1000, 3000, 5000])
        self.assertEqual(ex[1]["exclude"], [[0, 0, 0.2, 0.2]])
        for bad in ("1000:0.5,0.2,0.3,0.4", "1000:0.1,0.2,0.3", "1000:0.1,0.2,0.3,1.5"):
            with self.assertRaises(ValueError):
                vp.load_exemplars(None, [bad])

    def test_draw_reference_marks_boxes(self):
        img = np.full((168, 308, 3), 128, np.uint8)
        out = vp.draw_reference(img, [0.25, 0.25, 0.75, 0.75], [[0.0, 0.0, 0.2, 0.2]])
        self.assertEqual(tuple(out[42, 154]), (0, 220, 0))   # top edge of contact box
        self.assertEqual(tuple(out[0, 30]), (0, 0, 230))      # top edge of exclude box
        self.assertEqual(tuple(out[84, 154]), (128, 128, 128))  # inside untouched
        self.assertEqual(tuple(img[42, 154]), (128, 128, 128))  # input not modified

    def test_references_first_query_last_follow_adds_previous_answer(self):
        seen = []
        base = fake_model("norm1", fence=False)

        def post(payload):
            content = payload["messages"][0]["content"]
            if content[0]["text"] != vp.CALIB_PROMPT:
                seen.append((content[0]["text"], len(content) - 1))
            return base(payload)
        with tempfile.TemporaryDirectory() as d:
            ex = [{"t_ms": 3000, "contact": list(CONTACT), "exclude": []}]
            res = vp.probe_video(self._video(d), "fake", every_s=2.0, scenes=False,
                                 post_fn=post, log=lambda *_: None,
                                 exemplars=ex, follow=True)
        # 3000 ms is the reference itself and is not asked about.
        self.assertEqual([f["t_ms"] for f in res["frames"]], [1000, 5000])
        self.assertEqual(res["exemplar_t_ms"], [3000])
        self.assertTrue(res["follow"])
        # First query: 1 reference + query. Second: + yellow follow frame.
        self.assertEqual([n for _, n in seen], [2, 3])
        self.assertTrue(seen[0][0].startswith("The first 1 image(s)"))
        self.assertNotIn("YELLOW", seen[0][0])
        self.assertTrue(seen[1][0].startswith("The first 2 image(s)"))
        self.assertIn("YELLOW", seen[1][0])
        for f in res["frames"]:
            c = [b for b in f["boxes"] if b["label"] == "contact"][0]
            self.assertAlmostEqual(c["x0"], CONTACT[0], delta=0.01)

    def test_no_exemplars_keeps_single_image_prompt(self):
        counts = []
        base = fake_model("norm1", fence=False)

        def post(payload):
            counts.append(len(payload["messages"][0]["content"]) - 1)
            return base(payload)
        with tempfile.TemporaryDirectory() as d:
            res = vp.probe_video(self._video(d), "fake", every_s=2.0, scenes=False,
                                 post_fn=post, log=lambda *_: None)
        self.assertEqual(counts, [1, 1, 1, 1])  # calibration + 3 frames
        self.assertEqual(res["prompt"], vp.PROMPT)
        self.assertFalse(res["follow"])

    def test_exclude_label_kept(self):
        self.assertEqual(vp.normalize_label("exclude"), "exclude")


class ClipModeTest(unittest.TestCase):
    """Stage 2b: N frames over a few seconds in one request (video input)."""

    def _video(self, d):
        return ExemplarTest._video(self, d)

    def test_clip_times_end_at_keyframe_oldest_first(self):
        self.assertEqual(vp.clip_times_ms(10000, 4, 3000), [7000, 8000, 9000, 10000])
        self.assertEqual(vp.clip_times_ms(1000, 6, 3000), [0, 400, 1000])  # clamped, de-duplicated
        self.assertEqual(vp.clip_times_ms(5000, 1, 3000), [5000])

    def test_parse_clip_answer(self):
        text = ('```json\n{"scene_type": "Titjob", "moving": "Penis", "partner": "breast", '
                '"axis": "vertical", "people": "2", "boxes": [{"label": "contact", '
                '"bbox_2d": [0.4, 0.3, 0.6, 0.7]}]}\n```')
        status, boxes, clip = vp.parse_clip_answer(text, 1.0, 1.0)
        self.assertEqual(status, "ok")
        self.assertEqual(clip, {"scene_type": "titjob", "moving": "penis", "partner": "breasts",
                                "axis": "vertical", "people": 2})
        self.assertEqual(boxes[0]["label"], "contact")
        # Roles without boxes still count; unknown values become None.
        status, boxes, clip = vp.parse_clip_answer(
            '{"scene_type": "dancing", "moving": "elbow", "axis": "diagonal", "boxes": []}', 1, 1)
        self.assertEqual((status, boxes), ("empty", []))
        self.assertEqual((clip["scene_type"], clip["moving"], clip["axis"], clip["people"]),
                         (None, None, None, None))
        status, _, clip = vp.parse_clip_answer('{"scene_type": "blowjob", "moving": "mouth"}', 1, 1)
        self.assertEqual((status, clip["scene_type"]), ("ok", "blowjob"))
        self.assertEqual(vp.parse_clip_answer("Sorry, I can't help with that.", 1, 1)[0], "refused")
        self.assertIsNone(vp.parse_clip_answer("Sorry, I can't help with that.", 1, 1)[2])

    def test_probe_sends_sequence_and_records_roles(self):
        seen = []
        base = fake_model("norm1", fence=False)

        def post(payload):
            content = payload["messages"][0]["content"]
            if content[0]["text"] == vp.CALIB_PROMPT:
                return base(payload)
            seen.append((content[0]["text"], len(content) - 1))
            text = json.dumps({"scene_type": "blowjob", "moving": "mouth", "partner": "penis",
                               "axis": "vertical", "people": 1,
                               "boxes": [{"label": "contact", "bbox_2d": list(CONTACT)}]})
            return {"choices": [{"message": {"content": text}}]}
        with tempfile.TemporaryDirectory() as d:
            res = vp.probe_video(self._video(d), "fake", every_s=2.0, scenes=False,
                                 post_fn=post, log=lambda *_: None, clip=4, clip_span_s=1.5)
        self.assertEqual(res["clip"], 4)
        self.assertEqual(res["prompt"], vp.CLIP_PROMPT)
        # 1000 ms: frames 0..1000 (clamped, 3 distinct) ; later keyframes: all 4.
        self.assertEqual([n for _, n in seen], [3, 4, 4])
        self.assertTrue(seen[1][0].startswith("These 4 images are consecutive frames"))
        f = res["frames"][1]
        self.assertEqual(f["clip_ms"], [1500, 2000, 2500, 3000])
        self.assertEqual(f["clip"]["moving"], "mouth")
        self.assertAlmostEqual(f["boxes"][0]["x0"], CONTACT[0], delta=0.01)
        self.assertEqual(res["summary"]["scene_types"], {"blowjob": 3})

    def test_clip_and_exemplars_rejected(self):
        with self.assertRaises(ValueError):
            vp.probe_video("x.mp4", "fake", clip=4, exemplars=[{"t_ms": 0}], post_fn=lambda p: {})


if __name__ == "__main__":
    unittest.main()
