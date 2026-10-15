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
        self._playhead_ms: int | None = None
        self._bookmarks: list[int] = []
        self._chapters: list[tuple[int, int]] = []
        self.setMinimumHeight(120)

    def set_actions(self, actions: list[tuple[int, int]]) -> None:
        self._actions = list(actions)
        self._playhead_ms = None
        self.update()

    def set_marks(self, bookmarks: list[int], chapters: list[tuple[int, int]]) -> None:
        """Bookmark times and chapter (start, end) ranges, in script ms."""
        self._bookmarks = list(bookmarks)
        self._chapters = list(chapters)
        self.update()

    def set_playhead(self, ms: int | None) -> None:
        if ms != self._playhead_ms:
            self._playhead_ms = ms
            self.update()

    def x_for(self, ms: int, width: int) -> int | None:
        """Pixel column of script time ms (same scale as columns())."""
        if not self._actions or width <= 0:
            return None
        t0, t1 = self._actions[0][0], self._actions[-1][0]
        span = max(1, t1 - t0)
        return max(0, min(width - 1, (ms - t0) * (width - 1) // span))

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
        p.fillRect(self.rect(), QColor("#13151c"))
        w, h = self.width(), self.height()
        cols = self.columns(w)
        if not cols:
            p.setPen(QColor("#8a8f98"))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No script loaded")
            return
        for i, (a, b) in enumerate(self._chapters):
            xa, xb = self.x_for(a, w), self.x_for(b if b > a else a, w)
            band = QColor("#5ad4c4") if i % 2 == 0 else QColor("#b896e8")
            band.setAlpha(28)
            p.fillRect(xa, 0, max(2, xb - xa), h, band)
        p.setPen(QPen(QColor("#e0a86a"), 1, Qt.PenStyle.DashLine))
        for t in self._bookmarks:
            x = self.x_for(t, w)
            p.drawLine(QPointF(x, 0), QPointF(x, h))
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(QColor("#b896e8"), 1.5))
        y = lambda pos: (h - 4) - pos * (h - 8) / 100.0  # noqa: E731
        if len(cols) == len(self._actions):
            # Fewer points than pixels: the real line through every point.
            p.drawPolyline(QPolygonF([QPointF(x, y(lo)) for x, lo, _ in cols]))
        else:
            for x, lo, hi in cols:
                p.drawLine(QPointF(x, y(lo)), QPointF(x, y(hi)))
        if self._playhead_ms is not None:
            x = self.x_for(self._playhead_ms, w)
            p.setPen(QPen(QColor("#5ad4c4"), 2))
            p.drawLine(QPointF(x, 0), QPointF(x, h))
