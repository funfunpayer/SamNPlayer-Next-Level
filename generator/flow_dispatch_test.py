#!/usr/bin/env python3
"""Exercise the actual process_one Flow call with a recording backend.

Compile the call from production source so this dispatch regression can run
without OpenCV or video fixtures. Backend image scaling has separate tests.
"""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest


class FlowDispatchTest(unittest.TestCase):
    def test_cli_scale_reaches_backend(self):
        source = Path(__file__).with_name("generate_funscript.py")
        tree = ast.parse(source.read_text(encoding="utf-8"))
        process = next(n for n in tree.body
                       if isinstance(n, ast.FunctionDef) and n.name == "process_one")
        calls = [n for n in ast.walk(process)
                 if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)
                 and isinstance(n.func.value, ast.Name)
                 and n.func.value.id == "flow_backend" and n.func.attr == "analyze"]
        self.assertEqual(len(calls), 1)
        call = compile(ast.Expression(calls[0]), str(source), "eval")
        for requested, expected in [(0.5, 0.5), (0.25, 0.25), (0, 0.5),
                                    (1, 1.0), (-1, 0.5)]:
            with self.subTest(scale=requested):
                captured = {}
                def analyze(path, **kwargs):
                    captured.update(kwargs)
                    self.assertEqual(path, "input.mp4")
                args = SimpleNamespace(video="input.mp4", max_frames=30,
                    no_camera_compensation=True, axis="x", flow_downscale=requested)
                eval(call, {"args": args, "flow_backend": SimpleNamespace(analyze=analyze)})
                self.assertEqual(captured.get("downscale", 1.0), expected)
                self.assertEqual(captured["max_frames"], 30)
                self.assertFalse(captured["camera_compensation"])
                self.assertEqual(captured["axis"], "x")
                self.assertTrue(callable(captured["on_progress"]))

    def test_registry_flow_forwards_on_progress(self):
        """Plugin/registry path used to drop on_progress — progress stayed silent."""
        source = Path(__file__).with_name("generate_funscript.py").read_text(encoding="utf-8")
        self.assertIn("on_progress=options.get(\"on_progress\")", source)
        # Bound to the flow registry adapter, not only process_one.
        self.assertRegex(
            source,
            r"def flow\(video_path, roi, options\):[\s\S]*?on_progress=options\.get\(\"on_progress\"\)",
        )


if __name__ == "__main__":
    unittest.main()
