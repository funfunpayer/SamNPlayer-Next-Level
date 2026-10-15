"""Playback session logic without Qt: offset, loop, position on the curve.

Go reference: cmd/gui-wails/app_playback.go (SetScriptOffset,
ReportVideoPosition), app.go (findMatchingVideo).
"""

from __future__ import annotations

import bisect
import os
from dataclasses import dataclass, field

from ..library.script_finder import VIDEO_EXTS
from ..settings import Settings

OFFSET_LIMIT_MS = 10_000


def offset_key(script_path: str) -> str:
    return "playback.offset." + script_path


def clamp_offset(ms: int) -> int:
    return max(-OFFSET_LIMIT_MS, min(OFFSET_LIMIT_MS, int(ms)))


def script_time(video_ms: int, offset_ms: int) -> int:
    """The script time shown at video_ms (ReportVideoPosition: ms - offset, not below 0)."""
    return max(0, int(video_ms) - int(offset_ms))


def find_matching_video(script_path: str) -> str | None:
    """A video with the script's name beside it (findMatchingVideo)."""
    stem, _ = os.path.splitext(script_path)
    for ext in VIDEO_EXTS:
        cand = stem + ext
        if os.path.isfile(cand):
            return cand
    return None


def position_at(actions: list[tuple[int, int]], t_ms: int) -> float | None:
    """Interpolated position (0-100) at t_ms; None without points."""
    if not actions:
        return None
    times = [a for a, _ in actions]
    i = bisect.bisect_right(times, t_ms)
    if i == 0:
        return float(actions[0][1])
    if i >= len(actions):
        return float(actions[-1][1])
    (t0, p0), (t1, p1) = actions[i - 1], actions[i]
    if t1 == t0:
        return float(p1)
    return p0 + (p1 - p0) * (t_ms - t0) / (t1 - t0)


@dataclass
class LoopRange:
    start_ms: int
    end_ms: int

    def jump(self, video_ms: int) -> int | None:
        """Where to seek when video_ms has reached the end; None to keep playing."""
        if self.end_ms > self.start_ms and video_ms >= self.end_ms:
            return self.start_ms
        return None


@dataclass
class Session:
    settings: Settings
    script_path: str = ""
    video_path: str = ""
    offset_ms: int = 0
    loop: LoopRange | None = None
    actions: list[tuple[int, int]] = field(default_factory=list)

    def open_script(self, path: str, actions: list[tuple[int, int]], video: str | None = None) -> None:
        self.script_path = path
        self.actions = actions
        self.video_path = video if video is not None else (find_matching_video(path) or "")
        self.offset_ms = clamp_offset(self.settings.get(offset_key(path), 0) or 0)
        self.loop = None

    def set_offset(self, ms: int) -> int:
        self.offset_ms = clamp_offset(ms)
        if self.script_path:
            self.settings.set(offset_key(self.script_path), self.offset_ms)
        return self.offset_ms

    def script_ms(self, video_ms: int) -> int:
        return script_time(video_ms, self.offset_ms)
