"""Port of SamNPlayer funscript tests for bookmarks.go:
project_test.go (TestBookmarksRoundTrip, TestBookmarksAcceptFloatSeconds,
TestChaptersRoundTrip) and parse_compat_test.go
(TestLoadChaptersAndBookmarksTimestampStrings), same inputs/expectations.
The trailing tests are extra checks of helpers the Go code has no test for."""
import json
import os

import pytest

from samn.formats.marks import (Bookmark, Chapter, MarksError, flex_time_ms,
                                load_bookmarks, load_chapters,
                                normalize_time_to_ms, parse_clock_ms,
                                save_bookmarks, save_chapters)


# --- ported Go tests ------------------------------------------------------

def test_bookmarks_round_trip(tmp_path):
    path = tmp_path / "a.funscript"
    path.write_text('{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{"creator":"t"}}')
    save_bookmarks(path, [Bookmark("A", 100), Bookmark("B", 500)])
    out = load_bookmarks(path)
    assert len(out) == 2 and out[0].time_ms == 100 and out[1].name == "B"


def test_bookmarks_accept_float_seconds(tmp_path):
    path = tmp_path / "ofs.funscript"
    # OFS-style: fractional seconds in metadata
    path.write_text('{"actions":[{"at":0,"pos":0},{"at":2000,"pos":100}],"metadata":{"bookmarks":[{"name":"m","time":12.5}],"chapters":[{"name":"c","startTime":1.5,"endTime":3.25}]}}')
    bm = load_bookmarks(path)
    assert len(bm) == 1 and bm[0].time_ms == 12500, bm
    ch = load_chapters(path)
    assert len(ch) == 1 and ch[0].start_ms == 1500 and ch[0].end_ms == 3250, ch


def test_chapters_round_trip(tmp_path):
    path = tmp_path / "a.funscript"
    path.write_text('{"actions":[{"at":0,"pos":10},{"at":2000,"pos":90}]}')
    save_chapters(path, [Chapter("Intro", 0, 500)])
    ch = load_chapters(path)
    assert len(ch) == 1 and ch[0].name == "Intro"


def test_load_chapters_and_bookmarks_timestamp_strings(tmp_path):
    # OFS 3 writes chapter/bookmark times as timestamp text ("00:01:23.456").
    p = tmp_path / "x.funscript"
    p.write_text('''{"actions":[{"at":0,"pos":0},{"at":200000,"pos":100}],"metadata":{
	 "chapters":[{"name":"Intro","startTime":"00:00:00.000","endTime":"00:01:23.456"},
	             {"name":"Main","startTime":"1:23.456","endTime":"01:02:03"}],
	 "bookmarks":[{"name":"b","time":"00:00:12.5"}]}}''')
    ch = load_chapters(p)
    assert ch == [Chapter("Intro", 0, 83456), Chapter("Main", 83456, 3723000)]
    bm = load_bookmarks(p)
    assert len(bm) == 1 and bm[0].time_ms == 12500
    # Unreadable text stays an error.
    p.write_text('{"actions":[{"at":0,"pos":0}],"metadata":{"bookmarks":[{"name":"b","time":"soon"}]}}')
    with pytest.raises(MarksError):
        load_bookmarks(p)


# --- extra checks (no Go test) -------------------------------------------

def test_rounding_is_half_away_from_zero():
    assert normalize_time_to_ms(0.0005) == 1       # 0.5 ms -> 1 (Python round() gives 0)
    assert normalize_time_to_ms(2.5e-3) == 3       # 2.5 ms -> 3 (round() gives 2)
    assert normalize_time_to_ms(-0.0025) == -3
    assert normalize_time_to_ms(12.5) == 12500
    assert normalize_time_to_ms(2500.0) == 2500    # integral value stays ms
    assert normalize_time_to_ms(100000.5) == 100001  # >= 1e5 stays ms, rounded
    assert normalize_time_to_ms(float("nan")) == 0
    assert parse_clock_ms("0:00.0005") == 1


def test_parse_clock_ms_edges():
    assert parse_clock_ms("1:23.5") == 83500
    assert parse_clock_ms("01:02:03") == 3723000
    assert parse_clock_ms("12") is None
    assert parse_clock_ms("1.5:30") is None        # fraction only in seconds
    assert parse_clock_ms("1:2:3:4") is None
    assert parse_clock_ms("-1:30") is None
    assert parse_clock_ms("a:b") is None


def test_flex_time_values():
    assert flex_time_ms(None) == 0
    assert flex_time_ms(1500) == 1500
    assert flex_time_ms("1.5") == 1500
    assert flex_time_ms("00:00:12.5") == 12500
    for bad in (True, [], {}, "soon", "1e999", 1e999):
        with pytest.raises(MarksError):
            flex_time_ms(bad)


def test_save_sorts_and_preserves_other_keys(tmp_path):
    p = tmp_path / "s.funscript"
    p.write_text('{ "version": "1.0", "actions": [ {"at": 0, "pos": 1.0} ],\n'
                 ' "metadata": {"creator": "t", "chapters": [{"name":"x","startTime":5}], "z": 1.0} }')
    save_bookmarks(p, [Bookmark("late", 900), Bookmark("early", 100)])
    doc = json.loads(p.read_text())
    assert [b["name"] for b in doc["metadata"]["bookmarks"]] == ["early", "late"]
    assert doc["metadata"]["creator"] == "t"
    assert doc["metadata"]["chapters"] == [{"name": "x", "startTime": 5}]
    assert doc["version"] == "1.0"
    text = p.read_text()
    assert '"pos":1.0' in text and '"z":1.0' in text      # untouched values keep their text
    assert text.index('"actions"') < text.index('"metadata"') < text.index('"version"')


def test_save_chapters_omits_zero_end_and_sorts(tmp_path):
    p = tmp_path / "c.funscript"
    p.write_text('{"actions":[]}')
    save_chapters(p, [Chapter("b", 2000), Chapter("a", 0, 1000)])
    doc = json.loads(p.read_text())
    assert doc["metadata"]["chapters"] == [
        {"name": "a", "startTime": 0, "endTime": 1000}, {"name": "b", "startTime": 2000}]


def test_save_is_atomic_and_leaves_no_temp(tmp_path):
    p = tmp_path / "t.funscript"
    p.write_text('{"actions":[]}')
    save_bookmarks(p, [Bookmark("a", 1)])
    assert os.listdir(tmp_path) == ["t.funscript"]


def test_errors(tmp_path):
    with pytest.raises(MarksError):
        load_bookmarks(tmp_path / "missing.funscript")
    p = tmp_path / "bad.funscript"
    p.write_text("not json")
    with pytest.raises(MarksError):
        load_bookmarks(p)
    with pytest.raises(MarksError):
        save_bookmarks(p, [])
    p.write_text('{"metadata":{"bookmarks":{"a":1}}}')
    with pytest.raises(MarksError):
        load_bookmarks(p)
    p.write_text('{"metadata":null}')
    assert load_bookmarks(p) == [] and load_chapters(p) == []
