#!/usr/bin/env python3
"""ai_setup.py - checks (and, when asked, installs) everything the local AI
paths need on this PC, so the Owner has to do as little by hand as possible.

  python3 generator/ai_setup.py check            # report, changes nothing
  python3 generator/ai_setup.py check --json     # same, machine-readable (GUI)
  python3 generator/ai_setup.py install teachers # show the commands
  python3 generator/ai_setup.py install teachers --yes   # run them
  python3 generator/ai_setup.py install train --yes      # + PyTorch/RF-DETR
  python3 generator/ai_setup.py install models --yes     # ollama pull ...

What is checked (docs/VLM_MODELS.md, docs/LOCAL_MODEL_SETUP.md):
- Python, NVIDIA GPU (nvidia-smi), RAM, free disk, ffmpeg
- packages: opencv-contrib (CSRT), onnxruntime (+ CUDA provider), NudeNet,
  PyTorch (+ CUDA), RF-DETR
- local servers: Ollama (and which vision models are pulled), LM Studio,
  Colibri

Install profiles:
- teachers: onnxruntime(-gpu) + NudeNet. NudeNet is installed with
  --no-deps: its opencv-python-headless dependency would replace
  opencv-contrib-python and break CSRT tracking.
- train: a separate venv (LOCALAPPDATA or ~/.config /SamNPlayer/ai-train-venv)
  with PyTorch (CUDA on NVIDIA) + rfdetr[train] for contact_detector.py -
  separate because RF-DETR pulls opencv-python, which must never sit next
  to the app's opencv-contrib (CSRT).
- models: `ollama pull` of the recommended vision models for the GPU size.

Nothing runs without --yes; nothing is uploaded.
"""

import argparse
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
import urllib.request

TORCH_INDEX = "https://download.pytorch.org/whl/cu128"  # CUDA 12.8 wheels (RTX 20xx-50xx)


def train_venv_dir():
    """Separate Python for training: PyTorch + RF-DETR pull opencv-python via
    supervision, which must never land next to the app's opencv-contrib
    (CSRT). Training runs with this venv's python; the app is untouched."""
    base = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "SamNPlayer", "ai-train-venv")


def venv_python(venv):
    sub = ("Scripts", "python.exe") if platform.system() == "Windows" else ("bin", "python")
    return os.path.join(venv, *sub)
SERVERS = {
    "ollama": "http://127.0.0.1:11434",
    "lmstudio": "http://127.0.0.1:1234",
    "colibri": "http://127.0.0.1:8000",
    "colibri_app": "http://127.0.0.1:8080",  # SamNPlayer Settings default
}
# Vision models by GPU memory (Ollama tags; docs/VLM_MODELS.md).
VISION_MODELS = [
    (6, "qwen2.5vl:3b"),   # research licence - plumbing tests only
    (10, "qwen2.5vl:7b"),
    (10, "qwen3-vl:8b"),
    (24, "qwen2.5vl:32b"),
]


def _run(cmd, timeout=10):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout
    except (OSError, subprocess.SubprocessError):
        return None, ""


def parse_nvidia_smi(out):
    """nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits"""
    gpus = []
    for line in (out or "").strip().splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 3:
            try:
                gpus.append({"name": parts[0], "vram_gb": round(float(parts[1]) / 1024, 1),
                             "driver": parts[2]})
            except ValueError:
                continue
    return gpus


def check_gpu(run=_run):
    code, out = run(["nvidia-smi", "--query-gpu=name,memory.total,driver_version",
                     "--format=csv,noheader,nounits"])
    return parse_nvidia_smi(out) if code == 0 else []


def _has(module):
    try:
        return importlib.util.find_spec(module) is not None
    except (ImportError, ValueError):
        return False


def check_packages(has=_has):
    pk = {
        "opencv_contrib": False, "onnxruntime": has("onnxruntime"), "onnx_cuda": False,
        "nudenet": has("nudenet"), "torch": has("torch"), "torch_cuda": False,
        "rfdetr": has("rfdetr"),
    }
    if has("cv2"):
        try:
            import cv2
            pk["opencv_contrib"] = hasattr(cv2, "TrackerCSRT_create") or hasattr(
                getattr(cv2, "legacy", None), "TrackerCSRT_create")
        except Exception:  # noqa: BLE001 - a broken cv2 counts as missing
            pass
    if pk["onnxruntime"]:
        try:
            import onnxruntime
            pk["onnx_cuda"] = "CUDAExecutionProvider" in onnxruntime.get_available_providers()
        except Exception:  # noqa: BLE001
            pass
    if pk["torch"]:
        try:
            import torch
            pk["torch_cuda"] = bool(torch.cuda.is_available())
        except Exception:  # noqa: BLE001
            pass
    return pk


def probe_server(base, opener=urllib.request.urlopen):
    """(reachable, model ids) for an OpenAI-compatible or Ollama server."""
    for path, key in (("/api/tags", "models"), ("/v1/models", "data")):
        try:
            with opener(base + path, timeout=1.5) as r:
                data = json.loads(r.read().decode("utf-8") or "{}")
        except Exception:  # noqa: BLE001 - unreachable is the normal case
            continue
        items = data.get(key) or []
        return True, [str(i.get("name") or i.get("id")) for i in items if isinstance(i, dict)]
    return False, []


def check_system():
    total, _, free = shutil.disk_usage(os.path.expanduser("~"))
    ram_gb = None
    try:
        if hasattr(os, "sysconf") and "SC_PHYS_PAGES" in os.sysconf_names:
            ram_gb = round(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") / 2**30, 1)
    except (ValueError, OSError):
        pass
    if ram_gb is None and platform.system() == "Windows":  # pragma: no cover
        try:
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("l", ctypes.c_ulong), ("m", ctypes.c_ulong), ("t", ctypes.c_ulonglong),
                            ("a", ctypes.c_ulonglong), ("tp", ctypes.c_ulonglong),
                            ("ap", ctypes.c_ulonglong), ("tv", ctypes.c_ulonglong),
                            ("av", ctypes.c_ulonglong), ("ae", ctypes.c_ulonglong)]
            ms = MS()
            ms.l = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms))
            ram_gb = round(ms.t / 2**30, 1)
        except Exception:  # noqa: BLE001
            pass
    return {"os": platform.system(), "python": platform.python_version(),
            "ram_gb": ram_gb, "disk_free_gb": round(free / 2**30, 1),
            "ffmpeg": shutil.which("ffmpeg") is not None, "ollama_cli": shutil.which("ollama") is not None}


def recommended_models(vram_gb):
    return [m for need, m in VISION_MODELS if vram_gb and vram_gb >= need and need >= 10]


def advise(rep):
    """Human-readable next steps from a report."""
    tips = []
    pk, sysi, gpus = rep["packages"], rep["system"], rep["gpus"]
    if not pk["opencv_contrib"]:
        tips.append("OpenCV with CSRT missing: pip install opencv-contrib-python (not opencv-python)")
    if not pk["onnxruntime"] or not pk["nudenet"]:
        tips.append("teachers: python3 generator/ai_setup.py install teachers --yes")
    elif gpus and not pk["onnx_cuda"]:
        tips.append("onnxruntime runs on CPU only - install onnxruntime-gpu for the NVIDIA card")
    tv = rep.get("train_venv") or {}
    if not tv.get("rfdetr") or (gpus and not tv.get("torch_cuda")):
        tips.append("own detector training: python3 generator/ai_setup.py install train --yes"
                    + ("" if gpus else " (no NVIDIA GPU found - training on CPU is very slow)"))
    else:
        tips.append(f"train with: {tv['python']} generator/contact_detector.py train --dataset DS --out model.onnx")
    if not rep["servers"]["ollama"]["up"]:
        tips.append("no Ollama server - install Ollama (ollama.com) for the Qwen VLM teachers")
    else:
        have = set(rep["servers"]["ollama"]["models"])
        vram = max((g["vram_gb"] for g in gpus), default=0)
        missing = [m for m in recommended_models(vram) if m not in have]
        if missing:
            tips.append("vision models to pull: python3 generator/ai_setup.py install models --yes"
                        f" ({', '.join(missing)})")
    if not sysi["ffmpeg"]:
        tips.append("ffmpeg not on PATH (needed for AV1 clips and frame export)")
    if sysi["disk_free_gb"] is not None and sysi["disk_free_gb"] < 50:
        tips.append(f"only {sysi['disk_free_gb']} GB free - models and datasets need 50+ GB "
                    "(Colibri's large models: several hundred GB on a fast NVMe)")
    return tips


def check_train_venv(run=_run, venv=None):
    venv = venv or train_venv_dir()
    py = venv_python(venv)
    info = {"path": venv, "python": py, "exists": os.path.exists(py), "torch_cuda": False,
            "rfdetr": False}
    if info["exists"]:
        code, out = run([py, "-c", "import importlib.util as u, torch; "
                         "print(torch.cuda.is_available(), u.find_spec('rfdetr') is not None)"], timeout=60)
        if code == 0 and out.split():
            parts = out.split()
            info["torch_cuda"] = parts[0] == "True"
            info["rfdetr"] = len(parts) > 1 and parts[1] == "True"
    return info


def check(run=_run, has=_has, opener=urllib.request.urlopen, venv=None):
    rep = {"gpus": check_gpu(run), "packages": check_packages(has), "system": check_system(),
           "train_venv": check_train_venv(run, venv), "servers": {}}
    for name, base in SERVERS.items():
        up, models = probe_server(base, opener)
        rep["servers"][name] = {"url": base, "up": up, "models": models}
    rep["next_steps"] = advise(rep)
    return rep


def install_plan(profile, rep, python=sys.executable):
    pip = [python, "-m", "pip", "install"]
    gpus = rep["gpus"]
    cmds = []
    if profile in ("teachers", "all"):
        cmds.append(pip + ["onnxruntime-gpu" if gpus else "onnxruntime"])
        cmds.append(pip + ["--no-deps", "nudenet"])  # keep opencv-contrib (CSRT)
    if profile in ("train", "all"):
        venv = (rep.get("train_venv") or {}).get("path") or train_venv_dir()
        vpip = [venv_python(venv), "-m", "pip", "install"]
        cmds.append([python, "-m", "venv", venv])
        if gpus:
            cmds.append(vpip + ["torch", "torchvision", "--index-url", TORCH_INDEX])
        else:
            cmds.append(vpip + ["torch", "torchvision"])
        cmds.append(vpip + ["rfdetr[train]"])
    if profile in ("models", "all"):
        vram = max((g["vram_gb"] for g in gpus), default=0)
        for m in recommended_models(vram):
            cmds.append(["ollama", "pull", m])
    return cmds


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--json", action="store_true")
    i = sub.add_parser("install")
    i.add_argument("profile", choices=["teachers", "train", "models", "all"])
    i.add_argument("--yes", action="store_true", help="run the commands (default: only show them)")
    args = ap.parse_args(argv)

    rep = check()
    if args.cmd == "check":
        if args.json:
            print(json.dumps(rep, indent=1))
            return 0
        g = ", ".join(f"{x['name']} {x['vram_gb']} GB" for x in rep["gpus"]) or "none found"
        s = rep["system"]
        print(f"GPU: {g}\nPython {s['python']} on {s['os']}, RAM {s['ram_gb']} GB, "
              f"free disk {s['disk_free_gb']} GB, ffmpeg {'yes' if s['ffmpeg'] else 'no'}")
        for k, v in rep["packages"].items():
            print(f"  {k:15s} {'ok' if v else '--'}")
        tv = rep["train_venv"]
        print(f"  {'train venv':15s} {'ok' if tv['rfdetr'] else '--'} {tv['path']}"
              f"{' (CUDA)' if tv['torch_cuda'] else ''}")
        for k, v in rep["servers"].items():
            print(f"  {k:15s} {'up' if v['up'] else '--'} {v['url']} {', '.join(v['models'][:6])}")
        print("\nNext steps:" if rep["next_steps"] else "\nEverything needed is in place.")
        for t in rep["next_steps"]:
            print("  - " + t)
        return 0

    cmds = install_plan(args.profile, rep)
    for cmd in cmds:
        print("$ " + " ".join(cmd))
        if args.yes:
            if cmd[0] == "ollama" and not rep["system"]["ollama_cli"]:
                print("  skipped: ollama CLI not found")
                continue
            code = subprocess.call(cmd)
            if code != 0:
                print(f"  failed with exit code {code}", file=sys.stderr)
                return 1
    if not args.yes:
        print("\n(dry run - add --yes to run these)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
