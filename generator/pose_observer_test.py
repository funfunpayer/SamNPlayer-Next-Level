#!/usr/bin/env python3
"""Unit tests for PoseObserver Stage A — no model weights required."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pose_observer as po  # noqa: E402


def _fixture_obs():
    return po.parse_observation({
        "timestamp_ms": 1000,
        "frame_width": 200,
        "frame_height": 100,
        "backend": "fixture",
        "model": {"family": "fixture", "version": "t", "weights_id": "none"},
        "people": [{
            "person_id": 0,
            "confidence": 0.9,
            "landmarks": [
                {"name": "nose", "x_norm": 0.5, "y_norm": 0.2, "confidence": 0.95},
                {"name": "mouth_left", "x_norm": 0.45, "y_norm": 0.28, "confidence": 0.9},
                {"name": "mouth_right", "x_norm": 0.55, "y_norm": 0.28, "confidence": 0.9},
                {"name": "left_wrist", "x_norm": 0.2, "y_norm": 0.6, "confidence": 0.8},
                {"name": "left_index", "x_norm": 0.18, "y_norm": 0.62, "confidence": 0.7},
                {"name": "left_pinky", "x_norm": 0.22, "y_norm": 0.62, "confidence": 0.7},
                {"name": "right_wrist", "x_norm": 0.8, "y_norm": 0.6, "confidence": 0.8},
                {"name": "right_index", "x_norm": 0.82, "y_norm": 0.62, "confidence": 0.7},
                {"name": "right_pinky", "x_norm": 0.78, "y_norm": 0.62, "confidence": 0.7},
                {"name": "left_shoulder", "x_norm": 0.35, "y_norm": 0.35, "confidence": 0.85},
                {"name": "right_shoulder", "x_norm": 0.65, "y_norm": 0.35, "confidence": 0.85},
                {"name": "left_hip", "x_norm": 0.4, "y_norm": 0.7, "confidence": 0.85},
                {"name": "right_hip", "x_norm": 0.6, "y_norm": 0.7, "confidence": 0.85},
                {"name": "left_ear", "x_norm": 0.4, "y_norm": 0.18, "confidence": 0.6},
                {"name": "right_ear", "x_norm": 0.6, "y_norm": 0.18, "confidence": 0.6},
            ],
        }],
    })


class PoseObserverContractTests(unittest.TestCase):
    def test_parse_roundtrip_keys(self):
        obs = _fixture_obs()
        d = obs.to_dict()
        again = po.parse_observation(d)
        self.assertEqual(again.frame_width, 200)
        self.assertEqual(len(again.people), 1)
        self.assertEqual(len(again.people[0].landmarks), 15)

    def test_norm_to_pixel_clips(self):
        x, y, w, h = po.norm_to_pixel_box(0.9, 0.9, 0.5, 0.5, 100, 100)
        self.assertEqual(x, 90)
        self.assertEqual(y, 90)
        self.assertLessEqual(x + w, 100)
        self.assertLessEqual(y + h, 100)

    def test_seed_proposals_include_hands_mouth_and_weak_tip(self):
        seeds = po.observation_to_seed_proposals(_fixture_obs())
        ids = {s.class_id for s in seeds}
        self.assertIn("mouth", ids)
        self.assertIn("hand_1", ids)
        self.assertIn("hand_2", ids)
        self.assertIn("penis", ids)  # weak hip-midline proposal
        tip = next(s for s in seeds if s.class_id == "penis")
        self.assertLess(tip.confidence, 0.5)
        self.assertIn("hip_midline", tip.provenance)
        # Pixel boxes stay inside frame.
        for s in seeds:
            self.assertGreaterEqual(s.x, 0)
            self.assertGreaterEqual(s.y, 0)
            self.assertLessEqual(s.x + s.w, 200)
            self.assertLessEqual(s.y + s.h, 100)

    def test_missing_people_yields_no_seeds(self):
        obs = po.parse_observation({
            "timestamp_ms": 0, "frame_width": 10, "frame_height": 10,
            "people": [], "model": {"family": "x"},
        })
        self.assertEqual(po.observation_to_seed_proposals(obs), [])

    def test_mediapipe_missing_model_is_soft_error(self):
        import numpy as np
        frame = np.zeros((48, 64, 3), dtype="uint8")
        obs = po.observe_mediapipe(frame, model_path=None)
        self.assertTrue(obs.error)
        self.assertEqual(obs.people, [])

    def test_onnx_missing_model_is_soft_error(self):
        import numpy as np
        frame = np.zeros((48, 64, 3), dtype="uint8")
        obs = po.observe_onnx_keypoints(frame, model_path="/no/such/model.onnx")
        self.assertTrue(obs.error)
        self.assertEqual(obs.people, [])

    def test_jitter_zero_when_identical(self):
        obs = _fixture_obs()
        lms = obs.people[0].landmarks
        self.assertEqual(po.jitter_px(lms, lms, 200, 100), 0.0)

    def test_backend_status_keys(self):
        st = po.backend_status()
        for k in ("mediapipe_import", "mediapipe_model", "onnxruntime", "onnx_model"):
            self.assertIn(k, st)

    def test_multi_person_uses_highest_confidence(self):
        weak = {
            "person_id": 0, "confidence": 0.2,
            "landmarks": [
                {"name": "left_wrist", "x_norm": 0.1, "y_norm": 0.5, "confidence": 0.9},
                {"name": "left_index", "x_norm": 0.1, "y_norm": 0.5, "confidence": 0.9},
                {"name": "left_pinky", "x_norm": 0.1, "y_norm": 0.5, "confidence": 0.9},
            ],
        }
        strong = {
            "person_id": 1, "confidence": 0.95,
            "landmarks": [
                {"name": "left_wrist", "x_norm": 0.7, "y_norm": 0.5, "confidence": 0.9},
                {"name": "left_index", "x_norm": 0.72, "y_norm": 0.5, "confidence": 0.9},
                {"name": "left_pinky", "x_norm": 0.68, "y_norm": 0.5, "confidence": 0.9},
            ],
        }
        obs = po.parse_observation({
            "timestamp_ms": 0, "frame_width": 100, "frame_height": 100,
            "people": [weak, strong], "model": {"family": "fixture"},
        })
        seeds = po.observation_to_seed_proposals(obs)
        hand = next(s for s in seeds if s.class_id == "hand_1")
        self.assertGreater(hand.x, 50)  # from strong person at x≈0.7

    def test_malformed_partial_observation(self):
        obs = po.parse_observation({
            "people": [{"landmarks": [{"name": "nose"}]}],  # missing coords
            "model": {},
        })
        self.assertEqual(obs.frame_width, 0)
        self.assertEqual(len(obs.people), 1)
        self.assertEqual(obs.people[0].landmarks[0].x_norm, 0.0)
        self.assertEqual(po.observation_to_seed_proposals(obs), [])

    def test_confidence_filter_and_fuse_ranking(self):
        classical = [
            po.SeedProposal("penis", 10, 10, 20, 20, 0.8, "classical:motion"),
            po.SeedProposal("hand_1", 1, 1, 10, 10, 0.3, "classical:motion"),
        ]
        pose = [
            po.SeedProposal("hand_1", 50, 50, 10, 10, 0.9, "mediapipe:hand_1"),
            po.SeedProposal("penis", 12, 12, 20, 20, 0.2, "mediapipe:hip_midline_weak_tip"),
        ]
        fused = po.fuse_proposal_lists(classical, pose)
        self.assertEqual(fused[0].provenance, "mediapipe:hand_1")
        self.assertEqual(fused[1].provenance, "classical:motion")
        filtered = po.filter_seeds_by_confidence(fused, min_confidence=0.5)
        self.assertEqual([s.provenance for s in filtered], [
            "mediapipe:hand_1", "classical:motion",
        ])

    def test_summarize_metrics_rows(self):
        rows = [
            {"people": 1, "wall_time_ms": 10, "jitter_px": 1.0, "seed_count": 3,
             "backend": "mediapipe", "error": ""},
            {"people": 0, "wall_time_ms": 20, "jitter_px": 0.0, "seed_count": 0,
             "backend": "mediapipe", "error": "no pose"},
        ]
        s = po.summarize_metrics_rows(rows)
        self.assertEqual(s["frames"], 2)
        self.assertEqual(s["availability"], 0.5)
        self.assertEqual(s["mean_wall_time_ms"], 15.0)
        self.assertEqual(s["error_frames"], 1)
        self.assertEqual(s["backends"], ["mediapipe"])

    def test_unknown_backend_soft_error(self):
        import numpy as np
        frame = np.zeros((16, 16, 3), dtype="uint8")
        obs = po.observe(frame, backend="nope")
        self.assertIn("unknown backend", obs.error)
        self.assertEqual(obs.people, [])


if __name__ == "__main__":
    unittest.main()
