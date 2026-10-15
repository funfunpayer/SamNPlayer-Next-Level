"""The script curve: position (0-100) over time, drawn with QPainter.

Long scripts have far more points than pixels, so each pixel column draws
the min-max span of its points (level of detail) instead of every segment.
"""

from __future__ import annotations

from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QWidget


class CurveWidget(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._actions: list[tuple[int, int]] = []
        self.setMinimumHeight(120)

    def set_actions(self, actions: list[tuple[int, int]]) -> None:
        self._actions = list(actions)
        self.update()

    def columns(self, width: int) -> list[tuple[int, int, int]]:
        """(x, min pos, max pos) per pixel column that holds points."""
        if not self._actions or width <= 0:
            return []
        t0, t1 = self._actions[0][0], self._actions[-1][0]
        span = max(1, t1 - t0)
        cols: dict[int, list[int]] = {}
        for at, pos in self._actions:
            x = min(width - 1, (at - t0) * (width - 1) // span)
            c = cols.get(x)
            if c is None:
                cols[x] = [pos, pos]
            else:
                c[0] = min(c[0], pos)
                c[1] = max(c[1], pos)
        return [(x, lo, hi) for x, (lo, hi) in sorted(cols.items())]

    def paintEvent(self, _event) -> None:  # noqa: N802 (Qt name)
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#16181d"))
        w, h = self.width(), self.height()
        cols = self.columns(w)
        if not cols:
            p.setPen(QColor("#8a8f98"))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No script loaded")
            return
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(QColor("#4fb3ff"), 1.5))
        y = lambda pos: (h - 4) - pos * (h - 8) / 100.0  # noqa: E731
        if len(cols) == len(self._actions):
            # Fewer points than pixels: the real line through every point.
            p.drawPolyline(QPolygonF([QPointF(x, y(lo)) for x, lo, _ in cols]))
        else:
            for x, lo, hi in cols:
                p.drawLine(QPointF(x, y(lo)), QPointF(x, y(hi)))
