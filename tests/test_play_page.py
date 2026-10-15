import json

from samn.settings import Settings
from samn.ui.play_page import heatmap


def _script(path, n=41, step=250):
    path.write_text(json.dumps({"actions": [{"at": i * step, "pos": 0 if i % 2 else 100} for i in range(n)]}))
    return path


def test_script_finds_video_beside(page_factory, tmp_path):
    s = _script(tmp_path / "Clip.funscript")
    (tmp_path / "Clip.mp4").write_bytes(b"")
    page = page_factory(Settings(tmp_path / "s.json"))
    assert page.open_path(s)
    assert page.session.video_path == str(tmp_path / "Clip.mp4")
    assert page.player.source().toLocalFile() == str(tmp_path / "Clip.mp4")
    assert "Clip.mp4" in page.info.text() and "41 points" in page.info.text()


def test_video_finds_script_in_folder(page_factory, tmp_path):
    lib = tmp_path / "lib"
    lib.mkdir()
    _script(lib / "my clip.funscript")
    vid = tmp_path / "videos" / "My.Clip (2019) [1080p].mp4"
    vid.parent.mkdir()
    vid.write_bytes(b"")
    st = Settings(tmp_path / "s.json")
    st.set("library.script_folders", str(lib))
    page = page_factory(st)
    assert page.open_path(vid)
    assert page.script is not None and page.script.path.name == "my clip.funscript"
    assert page.session.video_path == str(vid)


def test_video_without_script_says_so(page_factory, tmp_path):
    vid = tmp_path / "lonely.mp4"
    vid.write_bytes(b"")
    page = page_factory(Settings(tmp_path / "s.json"))
    assert page.open_path(vid)
    assert page.script is None and "no script found" in page.info.text()


def test_playhead_follows_video_with_offset(page_factory, tmp_path):
    s = _script(tmp_path / "Clip.funscript")
    page = page_factory(Settings(tmp_path / "s.json"))
    page.open_path(s)
    page.offset.setValue(300)
    page.on_position(5000)
    assert page.curve._playhead_ms == 4700
    # Saved per script: a new page on the same settings gets it back.
    page2 = page_factory(Settings(tmp_path / "s.json"))
    page2.open_path(s)
    assert page2.offset.value() == 300


def test_loop_jumps_back(page_factory, tmp_path, monkeypatch):
    s = _script(tmp_path / "Clip.funscript")
    page = page_factory(Settings(tmp_path / "s.json"))
    page.open_path(s)
    seeks = []
    monkeypatch.setattr(page.player, "setPosition", seeks.append)
    monkeypatch.setattr(page.player, "position", lambda: 1000)
    page.loop_start.click()
    monkeypatch.setattr(page.player, "position", lambda: 3000)
    page.loop_end.click()
    page.on_position(2999)
    assert seeks == []
    page.on_position(3000)
    assert seeks == [1000]
    page.loop_clear.click()
    page.on_position(3500)
    assert seeks == [1000]


def test_heatmap_matches_mapper_buckets():
    acts = [(i * 250, 0 if i % 2 else 100) for i in range(41)]
    pts = heatmap(acts, 20)
    assert pts and all(0.0 <= v <= 1.0 for _, v in pts)
    assert pts[0][0] == 0 and pts[1][0] - pts[0][0] == 500  # 10 s / 20 buckets
    assert heatmap([(0, 0)], 20) == []


def test_variants_switch_and_keep_video(page_factory, tmp_path):
    vid = tmp_path / "Clip.mp4"
    vid.write_bytes(b"")
    _script(tmp_path / "Clip.funscript")
    _script(tmp_path / "Clip (Soft).funscript", n=11)
    page = page_factory(Settings(tmp_path / "s.json"))
    page.open_path(vid)
    assert page.variant.isVisibleTo(page) and page.variant.count() == 2
    names = [page.variant.itemText(i) for i in range(2)]
    assert "Main script" in names and "Soft" in names
    page.variant.activated.emit(names.index("Soft"))
    assert page.script.path.name == "Clip (Soft).funscript"
    assert page.session.video_path == str(vid)


def test_noscript_card_and_add_folder(page_factory, tmp_path):
    vid = tmp_path / "videos" / "Lonely.mp4"
    vid.parent.mkdir()
    vid.write_bytes(b"")
    page = page_factory(Settings(tmp_path / "s.json"))
    page.open_path(vid)
    assert page.noscript.isVisibleTo(page)
    assert not page.noscript_create.isEnabled()  # Generator not built yet: says so
    lib = tmp_path / "lib"
    lib.mkdir()
    page.add_script_folder(str(lib))
    assert "no script in that folder" in page.info.text()
    _script(lib / "lonely.funscript")
    page.add_script_folder(str(lib))
    assert page.script is not None and page.script.path.name == "lonely.funscript"
    assert not page.noscript.isVisibleTo(page)
    assert page.settings.get("library.script_folders") == str(lib)


def test_drop_video_and_script_together(page_factory, tmp_path):
    vid = tmp_path / "a.mp4"
    vid.write_bytes(b"")
    s = _script(tmp_path / "other name.funscript")
    page = page_factory(Settings(tmp_path / "s.json"))
    assert page.open_dropped([str(s), str(vid)])
    assert page.script.path.name == "other name.funscript"
    assert page.session.video_path == str(vid)
    assert not page.open_dropped(["/x/readme.txt"])
