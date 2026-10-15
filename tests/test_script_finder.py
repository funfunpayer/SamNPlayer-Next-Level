"""Port of cmd/gui-wails/app_open_media_test.go (+ limit checks)."""

from __future__ import annotations

from pathlib import Path

import pytest

from samn.library import script_finder
from samn.library.script_finder import (
    find_script_for_video,
    split_folders,
    variant_label,
    walk_script_folder,
)


def touch(p: Path) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("{}")


def test_find_script_for_video(tmp_path: Path) -> None:
    video = tmp_path / "Clip One.mp4"
    touch(video)

    assert not find_script_for_video(str(video), None).found

    # Axis companions do not count as the script.
    touch(tmp_path / "Clip One.surge.funscript")
    assert not find_script_for_video(str(video), None).found

    touch(tmp_path / "clip one.funscript")
    m = find_script_for_video(str(video), None)
    assert m.found and Path(m.script_path).name == "clip one.funscript"

    touch(tmp_path / "Clip One.funscript")
    found = Path(find_script_for_video(str(video), None).script_path).name
    if (tmp_path / "CLIP ONE.FUNSCRIPT").exists():
        # Case-insensitive file system (Windows, macOS): "Clip One.funscript"
        # is the same file as "clip one.funscript", which keeps its name.
        assert found == "clip one.funscript"
    else:
        assert found == "Clip One.funscript"

    touch(tmp_path / "Clip One.samn")
    assert Path(find_script_for_video(str(video), None).script_path).name == "Clip One.samn"

    assert not find_script_for_video("", None).found


def test_find_script_for_video_folders_names_variants(tmp_path: Path) -> None:
    videos = tmp_path / "videos"
    scripts = tmp_path / "scripts"
    videos.mkdir()
    scripts.mkdir()
    video = videos / "Some_Studio - Clip.Title (2019) [1080p].mp4"
    touch(video)

    # Only in the script folder, written differently, one level down.
    touch(scripts / "authorA" / "some studio clip title.funscript")
    m = find_script_for_video(str(video), [str(scripts)])
    assert m.found
    assert Path(m.script_path).name == "some studio clip title.funscript"
    assert not m.variants[0].beside

    # Variants: Soft, another author; not a sequel, not an axis file.
    touch(scripts / "Some Studio - Clip Title (Soft).funscript")
    touch(scripts / "authorB" / "Some Studio - Clip Title - by Bee.funscript")
    touch(scripts / "Some Studio - Clip Title 2.funscript")
    touch(scripts / "Some Studio - Clip Title.twist.funscript")
    m = find_script_for_video(str(video), [str(scripts)])
    labels = {v.label for v in m.variants}
    assert len(m.variants) == 3 and "" in labels and "soft" in labels and "by bee" in labels
    # the script with the video's own name comes first
    assert m.variants[0].label == ""

    # A script beside the video beats the folder.
    touch(videos / "Some_Studio - Clip.Title (2019) [1080p].funscript")
    m = find_script_for_video(str(video), [str(scripts)])
    assert m.variants[0].beside and Path(m.script_path).parent == videos

    # A .samn and its .funscript count once.
    touch(videos / "Some_Studio - Clip.Title (2019) [1080p].samn")
    m = find_script_for_video(str(video), [str(scripts)])
    assert Path(m.script_path).suffix == ".samn" and len(m.variants) == 4


@pytest.mark.parametrize(
    ("script", "video", "label", "ok"),
    [
        ("Clip", "Clip", "", True),
        ("clip [4K]", "Clip (2020)", "", True),
        ("Clip (Hard)", "Clip", "hard", True),
        ("Clip 10", "Clip 1", "", False),
        ("Clip 2", "Clip", "", False),
        ("Clip.roll", "Clip", "", False),
        ("Other", "Clip", "", False),
    ],
)
def test_variant_label(script: str, video: str, label: str, ok: bool) -> None:
    assert variant_label(script, video) == (label, ok)


def test_split_folders() -> None:
    assert split_folders(" /a \n\n/b\n/a\n") == ["/a", "/b"]


# --- extra checks for the limits (not in the Go tests) ---


def test_walk_depth_limit(tmp_path: Path) -> None:
    ok = tmp_path.joinpath(*"abcde") / "in.funscript"  # 5 levels below root
    deep = tmp_path.joinpath(*"abcdef") / "out.funscript"  # 6 levels: skipped
    touch(ok)
    touch(deep)
    found: list[str] = []
    walk_script_folder(str(tmp_path), found.append)
    assert [Path(p).name for p in found] == ["in.funscript"]


def test_walk_file_limit(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(script_finder, "SCRIPT_FOLDER_MAX_FILES", 3)
    for i in range(6):
        touch(tmp_path / f"f{i}.funscript")
    found: list[str] = []
    walk_script_folder(str(tmp_path), found.append)
    assert len(found) == 3
