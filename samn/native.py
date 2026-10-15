"""Loads the C++ modules built from native/ (CMake + pybind11).

The modules live in native/build (Linux) or native/build/Release (Windows)
unless SAMN_NATIVE_PATH names another directory. A missing module is an
error, never a silent fallback.
"""

from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent


def _search_paths() -> list[Path]:
    paths = []
    env = os.environ.get("SAMN_NATIVE_PATH")
    if env:
        paths.append(Path(env))
    paths += [_ROOT / "native" / "build", _ROOT / "native" / "build" / "Release"]
    return paths


def load(name: str):
    """Import native module `name` (e.g. "funscript_native")."""
    for p in _search_paths():
        if p.is_dir() and str(p) not in sys.path:
            sys.path.insert(0, str(p))
    try:
        return importlib.import_module(name)
    except ImportError as e:
        raise ImportError(
            f"{name} is not built: run cmake in native/ (see native/README.md)"
        ) from e
