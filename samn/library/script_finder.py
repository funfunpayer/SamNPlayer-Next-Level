"""Find the script(s) that belong to a video.

Port of the pure logic in ``cmd/gui-wails/app_open_media.go`` (Go version).

The search looks beside the video and in the user's script folders, tolerates
differently written names ("Clip (2019) [1080p]" <-> "clip") and returns every
variant of one video (Soft / Hard / another author), best first.
"""

from __future__ import annotations

import os
import re
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

VIDEO_EXTS: tuple[str, ...] = (
    ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".webm", ".ts", ".m2ts",
    ".flv", ".mpg", ".mpeg", ".3gp", ".ogv",
)
# Go name kept for easy cross-reference.
videoExts = VIDEO_EXTS

SCRIPT_EXTS: tuple[str, ...] = (".samn", ".funscript")

# Limits for walking a script folder: deep libraries stay responsive.
SCRIPT_FOLDER_MAX_DEPTH = 6
SCRIPT_FOLDER_MAX_FILES = 50000

# ASCII flag: Go's RE2 \s, \d, \b are ASCII-only.
_RE_BRACKETS = re.compile(r"[\[{][^\]}]*[\]}]", re.ASCII)
_RE_YEAR = re.compile(r"\((19|20)\d\d\)", re.ASCII)
_RE_SEP = re.compile(r"[\s._\-]+", re.ASCII)
# Release tags that differ between a video and its script's name.
_RE_TAGS = re.compile(
    r"\b(2160p|1440p|1080p|720p|480p|4k|8k|uhd|fhd|hd|x264|x265|h264|h265|hevc|av1"
    r"|60fps|30fps|vr180|vr|sbs|lr|180x180|3dh)\b",
    re.ASCII | re.IGNORECASE,
)

# Labels that name an axis file, not a variant (MultiFunPlayer / OFS names).
AXIS_LABELS: frozenset[str] = frozenset({
    "surge", "sway", "twist", "roll", "pitch",
    "l0", "l1", "l2", "r0", "r1", "r2",
    "v0", "v1", "v2", "a0", "a1", "a2",
    "vib", "vibe", "vibrate", "suck", "suction",
    "lube", "valve", "stroke", "raw",
})
axisLabels = AXIS_LABELS


@dataclass
class ScriptVariant:
    """One script that fits a video.

    ``label`` is "" for the script with the video's own name, else what
    follows it ("soft", "by someone", ...). ``beside``: it lies next to the video.
    """

    path: str
    label: str = ""
    beside: bool = False


@dataclass
class VideoScriptMatch:
    """The script found for a video (``found`` False: none yet).

    ``script_path`` is the best one; ``variants`` lists all, best first.
    """

    video_path: str
    script_path: str = ""
    found: bool = False
    variants: list[ScriptVariant] = field(default_factory=list)


def _go_ext(path: str) -> str:
    """Like Go's filepath.Ext: suffix from the last dot of the last element."""
    for i in range(len(path) - 1, -1, -1):
        c = path[i]
        if c == os.sep or (os.altsep and c == os.altsep):
            break
        if c == ".":
            return path[i:]
    return ""


def _go_base(path: str) -> str:
    return os.path.basename(path.rstrip("/\\") if len(path) > 1 else path) or (
        os.sep if path else "."
    )


def _go_dir(path: str) -> str:
    d = os.path.dirname(path)
    return os.path.normpath(d) if d else "."


def _trim_suffix(s: str, suffix: str) -> str:
    return s[: -len(suffix)] if suffix and s.endswith(suffix) else s


def split_folders(s: str) -> list[str]:
    """One path per line, trimmed, without blanks or duplicates."""
    out: list[str] = []
    seen: set[str] = set()
    for line in s.split("\n"):
        d = line.strip()
        if not d or d in seen:
            continue
        seen.add(d)
        out.append(d)
    return out


def normalize_title(stem: str) -> str:
    """Make names comparable: lower case, no brackets, years, release tags or separators."""
    s = stem.lower()
    s = _RE_BRACKETS.sub(" ", s)
    s = _RE_YEAR.sub(" ", s)
    s = _RE_SEP.sub(" ", s)
    s = _RE_TAGS.sub(" ", s)
    s = " ".join(s.split())
    return s.strip(" ()")


normalizeTitle = normalize_title


def variant_label(script_stem: str, video_stem: str) -> tuple[str, bool]:
    """Label of ``script_stem`` against ``video_stem``; ok=False when it does not belong."""
    if script_stem.casefold() == video_stem.casefold():
        return "", True
    s, v = normalize_title(script_stem), normalize_title(video_stem)
    if v == "":
        return "", False
    if s == v:
        return "", True
    if not s.startswith(v + " "):
        return "", False
    rest = s[len(v):].strip()
    rest = rest.strip(" ()-")
    if rest == "":
        return "", True
    # "Clip 2" is another clip, not a variant of "Clip"; an axis file is no
    # variant either. (Go's len() counts bytes.)
    if rest.strip("0123456789 ") == "" or rest in AXIS_LABELS or len(rest.encode()) > 40:
        return "", False
    return rest, True


variantLabel = variant_label


@dataclass
class _Cand:
    variant: ScriptVariant
    rank: int


def find_script_for_video(
    video_path: str | os.PathLike[str], folders: Iterable[str] | None = None
) -> VideoScriptMatch:
    """Look beside the video and in the script folders for its scripts."""
    video_path = os.fspath(video_path) if video_path else ""
    out = VideoScriptMatch(video_path=video_path)
    if video_path == "":
        return out
    video_dir = _go_dir(video_path)
    base = _go_base(video_path)
    video_stem = _trim_suffix(base, _go_ext(base))
    cands: list[_Cand] = []
    seen: set[str] = set()

    def consider(path: str, beside: bool) -> None:
        ext = _go_ext(path).lower()
        if ext not in SCRIPT_EXTS:
            return
        key = os.path.normpath(path).lower()
        if key in seen:
            return
        pbase = _go_base(path)
        stem = _trim_suffix(pbase, _go_ext(pbase))
        label, ok = variant_label(stem, video_stem)
        if not ok:
            return
        seen.add(key)
        # Lower rank = better: beside the video, the video's own name, exact
        # spelling, then the Emotion Script before .funscript.
        rank = 0
        if not beside:
            rank += 100
        if label != "":
            rank += 50
        if stem != video_stem:
            rank += 10
        if ext == ".funscript":
            rank += 2
        cands.append(_Cand(ScriptVariant(path=path, label=label, beside=beside), rank))

    try:
        with os.scandir(video_dir) as it:
            names = sorted((e.name, e.is_dir(follow_symlinks=False)) for e in it)
    except OSError:
        names = []
    for name, is_dir in names:
        if not is_dir:
            consider(os.path.normpath(os.path.join(video_dir, name)), True)

    for root in folders or ():
        walk_script_folder(root, lambda p: consider(p, _go_dir(p) == video_dir))

    cands.sort(key=lambda c: (c.rank, c.variant.label, c.variant.path))

    # One entry per variant: a .samn and its .funscript of the same name count
    # once (the better one, already first).
    by_name: set[str] = set()
    for c in cands:
        k = _trim_suffix(c.variant.path, _go_ext(c.variant.path)).lower()
        if k in by_name:
            continue
        by_name.add(k)
        out.variants.append(c.variant)
    if out.variants:
        out.script_path, out.found = out.variants[0].path, True
    return out


findScriptForVideo = find_script_for_video


def walk_script_folder(root: str, fn: Callable[[str], None]) -> None:
    """Call ``fn`` for every script under ``root``, within the depth and file
    limits; unreadable parts are skipped."""
    root = os.path.normpath(root)
    base_depth = root.count(os.sep)
    files = 0

    try:
        root_is_dir = os.path.isdir(root) and not os.path.islink(root)
        os.lstat(root)
    except OSError:
        return

    def visit_file(p: str) -> bool:
        """Return False to stop the whole walk (limit reached)."""
        nonlocal files
        files += 1
        if files > SCRIPT_FOLDER_MAX_FILES:
            return False
        if _go_ext(p).lower() in SCRIPT_EXTS:
            fn(p)
        return True

    def visit_dir(p: str) -> bool:
        if p.count(os.sep) - base_depth >= SCRIPT_FOLDER_MAX_DEPTH:
            return True  # skip this directory
        try:
            with os.scandir(p) as it:
                entries = sorted((e.name, e.is_dir(follow_symlinks=False)) for e in it)
        except OSError:
            return True
        for name, is_dir in entries:
            child = os.path.join(p, name)
            if is_dir:
                if not visit_dir(child):
                    return False
            elif not visit_file(child):
                return False
        return True

    if root_is_dir:
        visit_dir(root)
    else:
        visit_file(root)


walkScriptFolder = walk_script_folder
