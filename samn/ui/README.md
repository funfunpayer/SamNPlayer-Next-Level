# samn.ui

The Qt (PySide6) interface. It holds widgets only: no file parsing and no
device code. It calls `samn.formats`, `samn.library` and later
`samn.player` / `samn.devices`.

- **Main window:** mode switch (Play / Edit / Generate).
- **`curve.CurveWidget`:** script curve with level of detail.
- **Tests:** pytest-qt, `tests/test_main_window.py`.
- **Status:** P0.
