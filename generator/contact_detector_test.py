#!/usr/bin/env python3
"""Unit tests for contact_detector.py and the ONNX teacher in
contact_points.py - no PyTorch, no RF-DETR, no onnxruntime: fake P5c clip
folders, a fake `rfdetr` module and a fake ONNX session stand in."""

import json
import os
import sys
import tempfile
import types
import unittest

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import contact_detector as cd  # noqa: E402
import contact_points as cp  # noqa: E402


def make_clip(root, name, classes, labels):
    """P5c layout: <root>/<name>/reviewed_yolo/{images,labels,classes.json}.
    labels: one (class_id, cx, cy, w, h) or a list of them per frame (#326
    writes every reviewed box of one timestamp into one label file)."""
    d = os.path.join(root, name, "reviewed_yolo")
    os.makedirs(os.path.join(d, "images"))
    os.makedirs(os.path.join(d, "labels"))
    with open(os.path.join(d, "classes.json"), "w") as f:
        json.dump({"version": 1, "names": classes}, f)
    for i, row in enumerate(labels):
        stem = "mark_%04d_%d" % (i, i * 1000)
        cv2.imwrite(os.path.join(d, "images", stem + ".png"), np.zeros((36, 64, 3), np.uint8))
        with open(os.path.join(d, "labels", stem + ".txt"), "w") as f:
            for r in (row if isinstance(row, list) else [row]):
                f.write("%d %.3f %.3f %.3f %.3f\n" % r)
    return d


class DatasetTest(unittest.TestCase):
    def test_clip_split_and_class_remap(self):
        with tempfile.TemporaryDirectory() as t:
            root = os.path.join(t, "scene_map_learning")
            # Class ids differ per clip: "contact" is 0 in a, 1 in b and c.
            make_clip(root, "a", ["contact"], [(0, .5, .5, .2, .2)] * 3)
            make_clip(root, "b", ["glans", "contact"], [(1, .4, .6, .1, .1), (0, .5, .5, .1, .1)])
            make_clip(root, "c", ["glans", "contact"], [(1, .3, .3, .1, .1)] * 2)
            out = os.path.join(t, "ds")
            info = cd.build_dataset([root], out, log=lambda *_: None)
            self.assertEqual(info["names"], ["contact", "glans"])
            self.assertEqual(info["split_by"], "clip")
            self.assertEqual(sum(info["images"].values()), 7)
            # Every label file uses the global ids.
            lines = []
            for s in ("train", "valid"):
                for fn in os.listdir(os.path.join(out, s, "labels")):
                    with open(os.path.join(out, s, "labels", fn)) as f:
                        lines += [ln.split() for ln in f]
            self.assertEqual(sorted(int(ln[0]) for ln in lines), [0] * 6 + [1])
            # One clip's frames never sit on both sides of the split.
            for clip in ("c000", "c001", "c002"):
                sides = {s for s in ("train", "valid")
                         for fn in os.listdir(os.path.join(out, s, "images")) if fn.startswith(clip)}
                self.assertLessEqual(len(sides), 1, clip)
            with open(os.path.join(out, "data.yaml")) as f:
                y = f.read()
            self.assertIn("nc: 2", y)
            self.assertIn("  - contact\n  - glans\n", y)
            self.assertIn("val: valid/images", y)

    def test_multi_box_frame_keeps_every_box(self):
        with tempfile.TemporaryDirectory() as t:
            root = os.path.join(t, "scene_map_learning")
            make_clip(root, "a", ["glans", "contact"],
                      [[(1, .5, .5, .2, .2), (0, .3, .4, .1, .1)]] * 4)
            out = os.path.join(t, "ds")
            info = cd.build_dataset([root], out, log=lambda *_: None)
            self.assertEqual(sum(info["images"].values()), 4)
            for s in ("train", "valid"):
                for fn in os.listdir(os.path.join(out, s, "labels")):
                    with open(os.path.join(out, s, "labels", fn)) as f:
                        self.assertEqual(sorted(ln.split()[0] for ln in f), ["0", "1"])

    def test_class_filter_and_frame_split_for_few_clips(self):
        with tempfile.TemporaryDirectory() as t:
            root = os.path.join(t, "l")
            make_clip(root, "only", ["glans", "contact"], [(1, .5, .5, .2, .2)] * 10 + [(0, .5, .5, .2, .2)])
            logs = []
            info = cd.build_dataset([root], os.path.join(t, "ds"), classes=["contact"],
                                    valid_frac=0.3, log=logs.append)
            self.assertEqual(info["names"], ["contact"])
            self.assertEqual(info["split_by"], "frame")
            self.assertEqual(sum(info["images"].values()), 10)
            self.assertTrue(any("splitting by frame" in m for m in logs))

    def test_errors(self):
        with tempfile.TemporaryDirectory() as t:
            with self.assertRaises(ValueError):
                cd.build_dataset([t], os.path.join(t, "ds"), log=lambda *_: None)
            root = os.path.join(t, "l")
            make_clip(root, "x", ["glans"], [(0, .5, .5, .1, .1)] * 3)
            with self.assertRaises(ValueError):
                cd.build_dataset([root], os.path.join(t, "ds"), classes=["contact"], log=lambda *_: None)
        self.assertEqual(cd.main(["dataset", "--learning-dir", "/nonexistent", "--out", "/tmp/x"]), 2)


class TrainTest(unittest.TestCase):
    def test_train_calls_rfdetr_and_writes_model_card(self):
        calls = {}

        class FakeNano:
            def train(self, **kw):
                calls["train"] = kw

            def export(self, output_dir, format):
                calls["export"] = (output_dir, format)
                d = os.path.join(output_dir, "export")
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, "inference_model.onnx"), "wb") as f:
                    f.write(b"onnx")
                return d
        fake = types.ModuleType("rfdetr")
        fake.RFDETRNano = FakeNano
        sys.modules["rfdetr"] = fake
        try:
            with tempfile.TemporaryDirectory() as t:
                ds = os.path.join(t, "ds")
                os.makedirs(ds)
                with open(os.path.join(ds, "dataset.json"), "w") as f:
                    json.dump({"names": ["contact"], "images": {"train": 8, "valid": 2}}, f)
                out = os.path.join(t, "contact_detector.onnx")
                self.assertEqual(cd.main(["train", "--dataset", ds, "--out", out, "--epochs", "3",
                                          "--device", "cpu"]), 0)
                with open(out, "rb") as f:
                    self.assertEqual(f.read(), b"onnx")
                with open(cd.card_path(out)) as f:
                    card = json.load(f)
        finally:
            del sys.modules["rfdetr"]
        self.assertEqual(calls["train"]["dataset_dir"], ds)
        self.assertEqual(calls["train"]["epochs"], 3)
        self.assertEqual(calls["train"]["device"], "cpu")
        self.assertNotIn("resolution", calls["train"])
        self.assertEqual(calls["export"][1], "onnx")
        self.assertEqual(card["classes"], ["contact"])
        self.assertEqual(card["arch"], "rf-detr-nano")
        self.assertIn("Apache-2.0", card["licence"])

    def test_missing_rfdetr_is_a_clear_error(self):
        saved = sys.modules.pop("rfdetr", None)
        sys.modules["rfdetr"] = None  # makes `import rfdetr` raise ImportError
        try:
            with self.assertRaises(RuntimeError) as cm:
                cd._rfdetr_class("nano")
            self.assertIn("pip install", str(cm.exception))
        finally:
            del sys.modules["rfdetr"]
            if saved is not None:
                sys.modules["rfdetr"] = saved


class OnnxTeacherTest(unittest.TestCase):
    def test_decode_drops_background_and_thresholds(self):
        boxes = np.array([[[0.5, 0.5, 0.2, 0.4], [0.2, 0.2, 0.1, 0.1]]], np.float32)
        # 2 classes + background slot; query 1 is the stronger "contact" only
        # in the background column, which must be ignored.
        logits = np.array([[[3.0, -5.0, -9.0], [-2.0, -5.0, 9.0]]], np.float32)
        score, box = cp.decode_rfdetr(boxes, logits, 2, 0)
        self.assertGreater(score, 0.9)
        np.testing.assert_allclose(box, [0.4, 0.3, 0.6, 0.7], atol=1e-6)
        self.assertIsNone(cp.decode_rfdetr(boxes, logits, 2, 1))  # class 1 never > 0.3

    def test_model_runs_session_and_returns_point(self):
        seen = {}

        class IO:
            def __init__(self, name, shape=None):
                self.name, self.shape = name, shape

        class FakeSession:
            def get_inputs(self):
                return [IO("input", [1, 3, 32, 48])]

            def get_outputs(self):
                return [IO("dets"), IO("labels")]

            def run(self, names, feeds):
                seen["names"], seen["shape"] = names, feeds["input"].shape
                seen["mean"] = float(feeds["input"].mean())
                return (np.array([[[0.25, 0.5, 0.1, 0.2]]], np.float32),
                        np.array([[[5.0, -9.0]]], np.float32))
        with tempfile.TemporaryDirectory() as t:
            onnx = os.path.join(t, "m.onnx")
            with open(cd.card_path(onnx), "w") as f:
                json.dump({"classes": ["contact"]}, f)
            m = cp.OnnxContactModel(onnx, session=FakeSession())
            with self.assertRaises(ValueError):
                cp.OnnxContactModel(onnx, class_name="glans", session=FakeSession())
        frame = np.full((72, 128, 3), 255, np.uint8)
        x, y, rule, score, box = m.contact(frame, 128, 72)
        self.assertEqual(seen["names"], ["dets", "labels"])
        self.assertEqual(seen["shape"], (1, 3, 32, 48))
        # White frame after ImageNet normalization: mean of (1-m)/s ≈ 2.2.
        self.assertAlmostEqual(seen["mean"], np.mean([(1 - m_) / s for m_, s in
                                                      zip(cp._MEAN, cp._STD)]), places=4)
        self.assertEqual((round(x, 4), round(y, 4)), (0.25, 0.5))
        self.assertEqual(rule, "detector:contact")
        self.assertGreater(score, 0.99)


if __name__ == "__main__":
    unittest.main()
