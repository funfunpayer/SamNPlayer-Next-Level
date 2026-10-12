"""Companion .samn writer beside generated funscripts."""

import json
import os
import tempfile
import unittest

from samn_write import write_companion_samn, companion_samn_path


class SamnWriteTest(unittest.TestCase):
    def test_writes_tj_document(self):
        with tempfile.TemporaryDirectory() as td:
            fs = os.path.join(td, "clip.funscript")
            actions = [{"at": 0, "pos": 20}, {"at": 1000, "pos": 90}]
            meta = {"creator": "t", "duration": 1000, "quality_score": 0.9,
                    "quality_passed": True, "quality_warnings": []}
            path = write_companion_samn(
                fs, actions, meta, "tj", contact_vibration=True,
                contact_vibration_curve="soft")
            self.assertTrue(path.endswith(".samn"))
            self.assertEqual(path, companion_samn_path(fs))
            with open(path, encoding="utf-8") as fh:
                doc = json.load(fh)
            self.assertEqual(doc["kind"], "samnplayer.script")
            self.assertEqual(doc["playbackSource"], "recipe")
            self.assertEqual(len(doc["general"]), 2)
            self.assertEqual(doc["recipe"]["sync"], "suction_position")
            self.assertTrue(doc["recipe"]["contact_vibration"])
            self.assertEqual(len(doc["strengthPresets"]), 3)


if __name__ == "__main__":
    unittest.main()
