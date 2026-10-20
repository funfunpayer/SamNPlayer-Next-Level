"""A stand-in for uv in the tests (TrainController): `python install` and
`pip install` only note what was asked; `venv` makes a real virtual
environment (without pip) and `pip install` puts tiny torch and rfdetr
modules into it, so the controller's check really runs in it."""
import json
import os
import subprocess
import sys
from pathlib import Path

args = sys.argv[1:]
log = Path(os.environ["UV_CACHE_DIR"]) / "fake_uv.log"
log.parent.mkdir(parents=True, exist_ok=True)
with log.open("a", encoding="utf-8") as f:
    f.write(json.dumps({"args": args, "python_dir": os.environ.get("UV_PYTHON_INSTALL_DIR")}) + "\n")
if os.environ.get("FAKE_UV_FAIL") and os.environ["FAKE_UV_FAIL"] in args:
    print("error: no network here", flush=True)
    sys.exit(2)
if args[:1] == ["venv"]:
    subprocess.run([sys.executable, "-m", "venv", "--clear", "--without-pip", args[-1]], check=True)
elif args[:2] == ["pip", "install"]:
    py = Path(args[args.index("--python") + 1])
    out = subprocess.run([str(py), "-c", "import sysconfig; print(sysconfig.get_paths()['purelib'])"],
                         capture_output=True, text=True, check=True).stdout.strip()
    site = Path(out)
    (site / "torch").mkdir(parents=True, exist_ok=True)
    (site / "torch" / "__init__.py").write_text(
        "__version__ = '9.9.9'\nclass cuda:\n    @staticmethod\n    def is_available():\n        return False\n")
    (site / "rfdetr.py").write_text("")
print("done", flush=True)
