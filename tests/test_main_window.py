import json

from samn.ui.main_window import MainWindow


def test_mode_switch(qtbot):
    w = MainWindow()
    qtbot.addWidget(w)
    assert w.mode == "Play"
    w.mode_buttons.button(2).click()
    assert w.mode == "Generate"
    assert "not built yet" in w.pages.currentWidget().text()


def test_open_script_draws_curve(qtbot, tmp_path):
    p = tmp_path / "clip.funscript"
    acts = [{"at": i * 250, "pos": 0 if i % 2 else 100} for i in range(41)]
    p.write_text(json.dumps({"actions": acts}))
    w = MainWindow()
    qtbot.addWidget(w)
    assert w.play.open_path(p)
    assert "41 points" in w.play.info.text() and "10 s" in w.play.info.text()
    cols = w.play.curve.columns(400)
    assert cols[0] == (0, 100, 100) and cols[-1][0] == 399


def test_curve_level_of_detail():
    from samn.ui.curve import CurveWidget

    c = CurveWidget()
    c.set_actions([(i, (i * 37) % 101) for i in range(100_000)])
    cols = c.columns(500)
    assert len(cols) == 500  # one span per pixel, not 100 000 segments
    assert all(0 <= lo <= hi <= 100 for _, lo, hi in cols)


def test_broken_file_is_reported(qtbot, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox

    shown = []
    monkeypatch.setattr(QMessageBox, "warning", lambda *a: shown.append(a[2]))
    p = tmp_path / "bad.funscript"
    p.write_text("{not json")
    w = MainWindow()
    qtbot.addWidget(w)
    assert not w.play.open_path(p)
    assert shown and "bad.funscript" in shown[0]
