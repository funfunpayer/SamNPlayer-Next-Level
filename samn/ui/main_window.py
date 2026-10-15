"""Main window: the mode switch (Play / Edit / Generate) and the pages."""

from __future__ import annotations

from PySide6.QtWidgets import (
    QButtonGroup,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from ..settings import Settings
from .play_page import PlayPage

MODES = ("Play", "Edit", "Generate")


class NotYetPage(QLabel):
    """A mode that is not built yet says so plainly (no pretend features)."""

    def __init__(self, mode: str, phase: str) -> None:
        super().__init__(f"{mode} is not built yet in SamNPlayer 2 (phase {phase}, docs/PLAN.md).\n"
                         "Use SamNPlayer 0.7 for it until then.")
        self.setWordWrap(True)


class MainWindow(QMainWindow):
    def __init__(self, settings: Settings | None = None) -> None:
        super().__init__()
        self.setWindowTitle("SamNPlayer 2")
        self.resize(1100, 700)
        self.play = PlayPage(settings)
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

    def closeEvent(self, e) -> None:  # noqa: N802
        self.play.release_media()
        super().closeEvent(e)

    @property
    def mode(self) -> str:
        return MODES[self.pages.currentIndex()]
