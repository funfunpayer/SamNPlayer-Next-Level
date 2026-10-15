"""Funscript files, read through the C++ parser (native funscript_native)."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .. import native


@dataclass
class Funscript:
    path: Path
    actions: list[tuple[int, int]]  # (time ms, position 0-100), sorted
    inverted: bool
    dropped: int  # points dropped because their time was out of range

    @property
    def duration_ms(self) -> int:
        return self.actions[-1][0] if self.actions else 0


def load(path: str | Path) -> Funscript:
    fs = native.load("funscript_native")
    p = Path(path)
    s = fs.parse(p.read_text(encoding="utf-8"))
    return Funscript(
        path=p,
        actions=[(a.at, a.pos) for a in s.actions],
        inverted=s.inverted,
        dropped=s.dropped,
    )
