import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


@pytest.fixture(autouse=True)
def _own_config_dir(tmp_path, monkeypatch):
    """Every test gets its own settings folder (never the user's)."""
    monkeypatch.setenv("SAMN_CONFIG_DIR", str(tmp_path / "config"))


@pytest.fixture
def page_factory(qtbot):
    """PlayPage instances that release their media before Qt deletes them."""
    from samn.ui.play_page import PlayPage

    made = []

    def make(settings):
        p = PlayPage(settings)
        qtbot.addWidget(p)
        made.append(p)
        return p

    yield make
    for p in made:
        p.release_media()
        qtbot.wait(50)
