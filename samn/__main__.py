"""Start the app: python -m samn [script.funscript]"""

from __future__ import annotations

import sys


def main() -> int:
    from PySide6.QtWidgets import QApplication

    from .ui import theme
    from .ui.main_window import MainWindow

    app = QApplication(sys.argv)
    theme.apply(app)
    w = MainWindow()
    if len(sys.argv) > 1:
        w.play.open_path(sys.argv[1])
    w.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
