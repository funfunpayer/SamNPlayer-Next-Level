"""Main window: the mode switch (Play / Edit / Generate) and the pages."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtWidgets import (
    QButtonGroup,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from ..formats import funscript
from .curve import CurveWidget

MODES = ("Play", "Edit", "Generate")


class PlayPage(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.open_button = QPushButton("Open…")
        self.info = QLabel("Open a funscript to see its curve.")
        self.curve = CurveWidget()
        top = QHBoxLayout()
        top.addWidget(self.open_button)
        top.addWidget(self.info, 1)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.curve, 1)
        self.open_button.clicked.connect(self.pick)
        self.script: funscript.Funscript | None = None

    def pick(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Open script", "", "Funscript (*.funscript);;All files (*)"
        )
        if path:
            self.open_path(path)

    def open_path(self, path: str | Path) -> bool:
        try:
            s = funscript.load(path)
        except Exception as e:  # shown to the user, not swallowed
            QMessageBox.warning(self, "Open script", f"Could not open {Path(path).name}:\n{e}")
            return False
        self.script = s
        self.curve.set_actions(s.actions)
        secs = s.duration_ms / 1000
        note = f", {s.dropped} broken points skipped" if s.dropped else ""
        self.info.setText(f"{s.path.name}: {len(s.actions)} points, {secs:.0f} s{note}")
        return True


class NotYetPage(QLabel):
    """A mode that is not built yet says so plainly (no pretend features)."""

    def __init__(self, mode: str, phase: str) -> None:
        super().__init__(f"{mode} is not built yet in SamNPlayer 2 (phase {phase}, docs/PLAN.md).\n"
                         "Use SamNPlayer 0.7 for it until then.")
        self.setWordWrap(True)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("SamNPlayer 2")
        self.resize(1100, 700)
        self.play = PlayPage()
        self.pages = QStackedWidget()
        self.pages.addWidget(self.play)
        self.pages.addWidget(NotYetPage("Edit", "P4"))
        self.pages.addWidget(NotYetPage("Generate", "P3"))

        bar = QHBoxLayout()
        self.mode_buttons = QButtonGroup(self)
        for i, name in enumerate(MODES):
            b = QPushButton(name)
            b.setCheckable(True)
            b.setChecked(i == 0)
            self.mode_buttons.addButton(b, i)
            bar.addWidget(b)
        bar.addStretch(1)
        self.mode_buttons.idClicked.connect(self.pages.setCurrentIndex)

        root = QWidget()
        lay = QVBoxLayout(root)
        lay.addLayout(bar)
        lay.addWidget(self.pages, 1)
        self.setCentralWidget(root)

    @property
    def mode(self) -> str:
        return MODES[self.pages.currentIndex()]
