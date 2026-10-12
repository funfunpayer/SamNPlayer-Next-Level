#!/usr/bin/env python3
"""Unit tests for ai_setup.py - no GPU, no servers, no installs: runners,
package lookups and HTTP are faked."""

import io
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ai_setup as a  # noqa: E402


class FakeResp(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def opener_for(routes):
    def opener(url, timeout=None):
        if url in routes:
            return FakeResp(json.dumps(routes[url]).encode())
        raise OSError("refused")
    return opener


def gpu_run(out="NVIDIA GeForce RTX 4060 Ti, 16380, 560.94\n"):
    return lambda cmd, timeout=10: (0, out) if cmd[0] == "nvidia-smi" else (None, "")


class ParseTest(unittest.TestCase):
    def test_nvidia_smi(self):
        g = a.parse_nvidia_smi("NVIDIA GeForce RTX 4060 Ti, 16380, 560.94\nbad line\n")
        self.assertEqual(g, [{"name": "NVIDIA GeForce RTX 4060 Ti", "vram_gb": 16.0, "driver": "560.94"}])
        self.assertEqual(a.check_gpu(lambda c, timeout=10: (None, "")), [])

    def test_probe_server_ollama_and_openai(self):
        op = opener_for({"http://o/api/tags": {"models": [{"name": "qwen2.5vl:7b"}]},
                         "http://l/v1/models": {"data": [{"id": "internvl3_5-8b"}]}})
        self.assertEqual(a.probe_server("http://o", op), (True, ["qwen2.5vl:7b"]))
        self.assertEqual(a.probe_server("http://l", op), (True, ["internvl3_5-8b"]))
        self.assertEqual(a.probe_server("http://down", op), (False, []))

    def test_recommended_models_by_vram(self):
        self.assertEqual(a.recommended_models(8), [])
        self.assertEqual(a.recommended_models(16), ["qwen2.5vl:7b", "qwen3-vl:8b"])
        self.assertIn("qwen2.5vl:32b", a.recommended_models(32))


class CheckTest(unittest.TestCase):
    def test_fresh_pc_gets_all_next_steps(self):
        rep = a.check(run=gpu_run(), has=lambda m: False, opener=opener_for({}), venv="/nonexistent")
        tips = "\n".join(rep["next_steps"])
        self.assertEqual(rep["gpus"][0]["vram_gb"], 16.0)
        self.assertIn("opencv-contrib-python", tips)
        self.assertIn("install teachers", tips)
        self.assertIn("install train", tips)
        self.assertIn("Ollama", tips)

    def test_ready_pc_only_misses_models(self):
        # Only light modules report present, so no real torch/onnxruntime import.
        rep = a.check(run=gpu_run(), has=lambda m: m in ("nudenet", "rfdetr"), venv="/nonexistent",
                      opener=opener_for({"http://127.0.0.1:11434/api/tags": {"models": [{"name": "qwen2.5vl:7b"}]}}))
        rep["packages"].update(opencv_contrib=True, onnxruntime=True, onnx_cuda=True, nudenet=True)
        rep["train_venv"].update(torch_cuda=True, rfdetr=True)
        tips = a.advise(rep)
        self.assertTrue(any("qwen3-vl:8b" in t for t in tips), tips)
        self.assertFalse(any("install teachers" in t or "install train" in t for t in tips), tips)


class PlanTest(unittest.TestCase):
    def test_teachers_keep_opencv_contrib(self):
        cmds = a.install_plan("teachers", {"gpus": [{"vram_gb": 16}]}, python="py")
        self.assertIn(["py", "-m", "pip", "install", "onnxruntime-gpu"], cmds)
        self.assertIn(["py", "-m", "pip", "install", "--no-deps", "nudenet"], cmds)
        cpu = a.install_plan("teachers", {"gpus": []}, python="py")
        self.assertIn(["py", "-m", "pip", "install", "onnxruntime"], cpu)

    def test_train_goes_into_separate_venv_with_cuda_torch(self):
        rep = {"gpus": [{"vram_gb": 16}], "train_venv": {"path": "/v"}}
        cmds = a.install_plan("train", rep, python="py")
        vpy = a.venv_python("/v")
        self.assertEqual(cmds[0], ["py", "-m", "venv", "/v"])
        self.assertEqual(cmds[1][:3], [vpy, "-m", "pip"])
        self.assertIn(a.TORCH_INDEX, cmds[1])
        self.assertEqual(cmds[2], [vpy, "-m", "pip", "install", "rfdetr[train]"])
        # Nothing of the training stack touches the app's python.
        self.assertFalse(any(c[0] == "py" and "install" in c for c in cmds))

    def test_train_venv_check(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(a.check_train_venv(venv=d)["exists"])
            py = a.venv_python(d)
            os.makedirs(os.path.dirname(py))
            open(py, "w").close()
            info = a.check_train_venv(run=lambda c, timeout=10: (0, "True True\n"), venv=d)
        self.assertTrue(info["exists"] and info["torch_cuda"] and info["rfdetr"])

    def test_models_by_gpu(self):
        cmds = a.install_plan("models", {"gpus": [{"vram_gb": 16}]})
        self.assertEqual(cmds, [["ollama", "pull", "qwen2.5vl:7b"], ["ollama", "pull", "qwen3-vl:8b"]])


if __name__ == "__main__":
    unittest.main()
