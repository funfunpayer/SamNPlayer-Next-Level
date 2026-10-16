"""All parts of the core load into one Python process (one samn_native)."""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build"))

try:
    import samn_native
    from samn_native import beat_strokes, beatbar, funscript, stroke_filter
except ImportError as e:
    print("SKIP: samn_native not built", file=sys.stderr)
    print(e, file=sys.stderr)
    sys.exit(1 if os.environ.get("SAMN_NATIVE_REQUIRED") else 0)


def main():
    a = samn_native.Action(100, 50)
    # One Action type everywhere: a parsed script's actions feed the filters.
    assert funscript.Action is samn_native.Action is stroke_filter.Action is beat_strokes.Action
    s = funscript.parse('{"actions":[{"at":0,"pos":0},{"at":100,"pos":50}]}')
    assert [(x.at, x.pos) for x in s.actions] == [(0, 0), (100, 50)]
    assert isinstance(s.actions[1], type(a))
    assert beatbar is not None
    print("ok: one module, one Action type")


if __name__ == "__main__":
    main()
