from samn.player.session import (
    LoopRange, Session, clamp_offset, find_matching_video, position_at, script_time,
)
from samn.settings import Settings


def test_script_time_and_clamp():
    assert script_time(5000, 300) == 4700
    assert script_time(100, 300) == 0  # never below 0, as in Go
    assert script_time(1000, -500) == 1500
    assert clamp_offset(20_000) == 10_000 and clamp_offset(-99_999) == -10_000


def test_position_at():
    a = [(0, 0), (1000, 100), (2000, 0)]
    assert position_at(a, -5) == 0
    assert position_at(a, 500) == 50
    assert position_at(a, 1500) == 50
    assert position_at(a, 9999) == 0
    assert position_at([], 10) is None


def test_loop():
    loop = LoopRange(1000, 3000)
    assert loop.jump(2999) is None
    assert loop.jump(3000) == 1000
    assert LoopRange(3000, 1000).jump(5000) is None


def test_matching_video(tmp_path):
    s = tmp_path / "Clip.funscript"
    s.write_text("{}")
    assert find_matching_video(str(s)) is None
    v = tmp_path / "Clip.mkv"
    v.write_bytes(b"")
    assert find_matching_video(str(s)) == str(v)


def test_offset_saved_per_script(tmp_path):
    st = Settings(tmp_path / "settings.json")
    s = Session(st)
    s.open_script("/a.funscript", [(0, 0)], video="")
    assert s.set_offset(25_000) == 10_000
    # Reopened later (new settings object, same file): the offset is back.
    s2 = Session(Settings(tmp_path / "settings.json"))
    s2.open_script("/a.funscript", [(0, 0)], video="")
    assert s2.offset_ms == 10_000
    s2.open_script("/b.funscript", [(0, 0)], video="")
    assert s2.offset_ms == 0


def test_broken_settings_file_is_kept_aside(tmp_path):
    p = tmp_path / "settings.json"
    p.write_text("{oops")
    st = Settings(p)
    assert st.get("x") is None
    assert (tmp_path / "settings.broken.json").exists()
    st.set("x", 1)
    assert Settings(p).get("x") == 1
