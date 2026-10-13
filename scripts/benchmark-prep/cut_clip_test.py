#!/usr/bin/env python3
"""Unit tests for cut_clip.py (no real video required)."""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cut_clip  # noqa: E402


class ParseTimeTests(unittest.TestCase):
    def test_seconds_and_hms(self):
        self.assertEqual(cut_clip.parse_time_to_seconds(90), 90.0)
        self.assertEqual(cut_clip.parse_time_to_seconds("90"), 90.0)
        self.assertEqual(cut_clip.parse_time_to_seconds("1:30"), 90.0)
        self.assertEqual(cut_clip.parse_time_to_seconds("00:01:30"), 90.0)
        self.assertAlmostEqual(cut_clip.parse_time_to_seconds("1:30.5"), 90.5)


class FFmpegArgsTests(unittest.TestCase):
    def test_scale_never_upscales_pattern(self):
        self.assertEqual(
            cut_clip.build_scale_filter(1280),
            "scale='min(1280,iw)':-2:flags=lanczos",
        )

    def test_argv_matches_proxy_style(self):
        args = cut_clip.build_ffmpeg_args(
            input_path="in.mp4",
            output_path="out.mp4",
            start_sec=10,
            end_sec=55,
            max_width=1280,
            crf=20,
        )
        self.assertEqual(args[0], "ffmpeg")
        self.assertIn("-ss", args)
        self.assertIn("10", args)
        self.assertIn("-t", args)
        self.assertIn("45", args)
        self.assertIn("-vf", args)
        self.assertIn("scale='min(1280,iw)':-2:flags=lanczos", args)
        self.assertIn("libx264", args)
        self.assertIn("20", args)
        self.assertIn("yuv420p", args)
        self.assertIn("+faststart", args)

    def test_no_audio(self):
        args = cut_clip.build_ffmpeg_args(
            input_path="in.mp4",
            output_path="out.mp4",
            start_sec=0,
            duration_sec=5,
            audio=False,
        )
        self.assertIn("-an", args)
        self.assertNotIn("aac", args)

    def test_rejects_bad_window(self):
        with self.assertRaises(ValueError):
            cut_clip.build_ffmpeg_args(
                input_path="in.mp4", output_path="out.mp4", start_sec=10, end_sec=5
            )


class MarksTests(unittest.TestCase):
    def test_load_and_window(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "marks.json"
            path.write_text(
                json.dumps(
                    {
                        "source": "/vid/long.mp4",
                        "max_width": 960,
                        "clips": [
                            {"name": "a", "start": "0:10", "end": "0:40"},
                            {"name": "b", "start_sec": 50, "duration_sec": 20},
                        ],
                    }
                ),
                encoding="utf-8",
            )
            marks = cut_clip.load_marks(path)
            self.assertEqual(marks["source"], "/vid/long.mp4")
            n, s, e = cut_clip.clip_window(marks["clips"][0])
            self.assertEqual((n, s, e), ("a", 10.0, 40.0))
            n, s, e = cut_clip.clip_window(marks["clips"][1])
            self.assertEqual((n, s, e), ("b", 50.0, 70.0))

    def test_dry_run_marks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "marks.json"
            path.write_text(
                json.dumps(
                    {
                        "source": "long.mp4",
                        "clips": [{"name": "x", "start": 0, "duration": 3}],
                    }
                ),
                encoding="utf-8",
            )
            rc = cut_clip.main(
                ["--marks", str(path), "--out-dir", tmp, "--dry-run", "--preset-res", "960w"]
            )
            self.assertEqual(rc, 0)


class IntegrationFFmpegTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg not available")
    def test_cut_synthetic(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "src.mp4"
            out = Path(tmp) / "clip.mp4"
            # 2s 320x240 color bars — then request max_width 640 (must not upscale).
            gen = [
                "ffmpeg",
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=c=blue:s=320x240:d=2",
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                str(src),
            ]
            self.assertEqual(cut_clip.subprocess.run(gen).returncode, 0)
            rc = cut_clip.main(
                [
                    "-i",
                    str(src),
                    "-o",
                    str(out),
                    "--start-sec",
                    "0",
                    "--duration",
                    "1",
                    "--max-width",
                    "640",
                    "--no-audio",
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(out.is_file() and out.stat().st_size > 500)


if __name__ == "__main__":
    unittest.main()
