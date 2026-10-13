#!/usr/bin/env python3
"""Cut + downscale short clips for Benchmark / KI training prep.

Owner tool (not Everyday Create). Reuses the same soft Lanczos downscale
pattern as videox/playable.go (libx264, CRF, never upscale):

  scale='min(MAX_W,iw)':-2:flags=lanczos

Default MAX_W=1280 (~720p landscape) — matches golden clips
(clip_ausschnitt / clip_voll at 1280×720) where Everyday Go CSRT is
measured. Prefer 960-wide when disk/speed matter and the tip stays clear.

Usage:
  python3 cut_clip.py -i long.mp4 --start 01:20 --end 02:10 -o clip.mp4
  python3 cut_clip.py -i long.mp4 --start-sec 80 --duration 45 -o clip.mp4
  python3 cut_clip.py --marks marks.json --out-dir ./clips
"""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

# Defaults aligned with Everyday golden clips + videox SoftProxy encode.
DEFAULT_MAX_WIDTH = 1280
DEFAULT_CRF = 20
DEFAULT_PRESET = "veryfast"
DEFAULT_AUDIO_BITRATE = "128k"
# Soft proxy in-app caps at 1920; training/bench clips stay smaller.
PROXY_STYLE_MAX_WIDTH = 1920


def parse_time_to_seconds(value: str | float | int) -> float:
    """Accept seconds (number/string) or HH:MM:SS[.ms] / MM:SS[.ms]."""
    if isinstance(value, (int, float)):
        return float(value)
    s = str(value).strip()
    if not s:
        raise ValueError("empty time")
    if ":" not in s:
        return float(s)
    parts = s.split(":")
    if len(parts) == 2:
        m, sec = parts
        return int(m) * 60 + float(sec)
    if len(parts) == 3:
        h, m, sec = parts
        return int(h) * 3600 + int(m) * 60 + float(sec)
    raise ValueError(f"bad time {value!r}")


def format_seconds(sec: float) -> str:
    if sec < 0:
        raise ValueError("negative time")
    # ffmpeg accepts fractional seconds as plain float strings
    if abs(sec - round(sec)) < 1e-6:
        return str(int(round(sec)))
    return f"{sec:.3f}".rstrip("0").rstrip(".")


def build_scale_filter(max_width: int) -> str:
    """Never upscale; keep aspect; even height via -2 (yuv420p-safe)."""
    if max_width <= 0:
        raise ValueError("max_width must be > 0")
    return f"scale='min({int(max_width)},iw)':-2:flags=lanczos"


def build_ffmpeg_args(
    *,
    input_path: str,
    output_path: str,
    start_sec: float,
    end_sec: float | None = None,
    duration_sec: float | None = None,
    max_width: int = DEFAULT_MAX_WIDTH,
    crf: int = DEFAULT_CRF,
    preset: str = DEFAULT_PRESET,
    audio: bool = True,
    audio_bitrate: str = DEFAULT_AUDIO_BITRATE,
    overwrite: bool = True,
) -> list[str]:
    """Pure argv builder (no subprocess) — unit-testable without ffmpeg."""
    if start_sec < 0:
        raise ValueError("start_sec must be >= 0")
    if end_sec is not None and duration_sec is not None:
        raise ValueError("pass end or duration, not both")
    if end_sec is None and duration_sec is None:
        raise ValueError("need end or duration")
    if end_sec is not None:
        if end_sec <= start_sec:
            raise ValueError("end must be after start")
        duration_sec = end_sec - start_sec
    if duration_sec is None or duration_sec <= 0:
        raise ValueError("duration must be > 0")

    args = ["ffmpeg"]
    if overwrite:
        args.append("-y")
    args += [
        "-hide_banner",
        "-loglevel",
        "error",
        # Input seek first (fast); accurate enough for bench clips.
        "-ss",
        format_seconds(start_sec),
        "-i",
        input_path,
        "-t",
        format_seconds(duration_sec),
        "-vf",
        build_scale_filter(max_width),
        "-c:v",
        "libx264",
        "-preset",
        preset,
        "-crf",
        str(crf),
        "-pix_fmt",
        "yuv420p",
    ]
    if audio:
        args += ["-c:a", "aac", "-b:a", audio_bitrate]
    else:
        args += ["-an"]
    args += ["-movflags", "+faststart", output_path]
    return args


def load_marks(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("marks file must be a JSON object")
    clips = data.get("clips")
    if not isinstance(clips, list) or not clips:
        raise ValueError("marks file needs a non-empty 'clips' list")
    source = data.get("source") or data.get("video")
    if not source:
        raise ValueError("marks file needs 'source' (or 'video') path")
    return {"source": source, "clips": clips, "max_width": data.get("max_width")}


def clip_window(entry: dict) -> tuple[str, float, float]:
    """Return (name, start_sec, end_sec) from one marks entry."""
    name = entry.get("name") or entry.get("id")
    if not name:
        raise ValueError("each clip needs 'name'")

    if "start_sec" in entry or "end_sec" in entry:
        if "start_sec" in entry:
            start = parse_time_to_seconds(entry["start_sec"])
        else:
            start = parse_time_to_seconds(entry.get("start", 0))
        if "end_sec" in entry:
            end = parse_time_to_seconds(entry["end_sec"])
        elif "duration_sec" in entry:
            end = start + parse_time_to_seconds(entry["duration_sec"])
        elif "duration" in entry:
            end = start + parse_time_to_seconds(entry["duration"])
        else:
            raise ValueError(f"clip {name!r}: need end_sec or duration")
        return str(name), start, end

    start = parse_time_to_seconds(entry.get("start", entry.get("in", 0)))
    if "end" in entry or "out" in entry:
        end_raw = entry["end"] if "end" in entry else entry["out"]
        end = parse_time_to_seconds(end_raw)
    elif "duration" in entry:
        end = start + parse_time_to_seconds(entry["duration"])
    else:
        raise ValueError(f"clip {name!r}: need end/out or duration")
    return str(name), start, end


def run_ffmpeg(args: list[str], dry_run: bool = False) -> None:
    if dry_run:
        print(shlex.join(args))
        return
    if shutil.which("ffmpeg") is None and args[0] == "ffmpeg":
        raise SystemExit(
            "ffmpeg not found on PATH (portable zip / Settings → Install "
            "video tools / apt install ffmpeg)"
        )
    proc = subprocess.run(args, capture_output=True, text=True)
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip() or f"exit {proc.returncode}"
        raise SystemExit(f"ffmpeg failed: {detail}")


def export_one(
    *,
    source: str,
    out_path: Path,
    start_sec: float,
    end_sec: float,
    max_width: int,
    crf: int,
    preset: str,
    audio: bool,
    dry_run: bool,
) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    args = build_ffmpeg_args(
        input_path=source,
        output_path=str(out_path),
        start_sec=start_sec,
        end_sec=end_sec,
        max_width=max_width,
        crf=crf,
        preset=preset,
        audio=audio,
    )
    print(
        f"cut {format_seconds(start_sec)}→{format_seconds(end_sec)}s "
        f"max_w={max_width} → {out_path}"
    )
    run_ffmpeg(args, dry_run=dry_run)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Cut + Lanczos-downscale short clips for Benchmark / KI prep"
    )
    p.add_argument("-i", "--input", help="Source video")
    p.add_argument("-o", "--output", help="Output mp4 (single-clip mode)")
    p.add_argument("--start", help="Start time (sec or HH:MM:SS)")
    p.add_argument("--end", help="End time (sec or HH:MM:SS)")
    p.add_argument("--start-sec", type=float, help="Start seconds")
    p.add_argument("--end-sec", type=float, help="End seconds")
    p.add_argument("--duration", type=float, help="Duration seconds from start")
    p.add_argument(
        "--max-width",
        type=int,
        default=None,
        help=f"Cap width, keep aspect, never upscale (default {DEFAULT_MAX_WIDTH})",
    )
    p.add_argument(
        "--preset-res",
        choices=("720p", "960w", "1080p"),
        help="Shortcut: 720p→1280, 960w→960, 1080p→1920 (still never upscales)",
    )
    p.add_argument("--crf", type=int, default=DEFAULT_CRF)
    p.add_argument("--x264-preset", default=DEFAULT_PRESET)
    p.add_argument("--no-audio", action="store_true")
    p.add_argument("--marks", help="Batch marks JSON (see example_marks.json)")
    p.add_argument("--out-dir", help="Output directory for --marks")
    p.add_argument("--dry-run", action="store_true", help="Print ffmpeg argv only")
    return p


def resolve_max_width(args: argparse.Namespace, marks_default=None) -> int:
    """CLI --preset-res / --max-width win over marks.json max_width."""
    if args.preset_res:
        return {"720p": 1280, "960w": 960, "1080p": PROXY_STYLE_MAX_WIDTH}[args.preset_res]
    if args.max_width is not None:
        return int(args.max_width)
    if marks_default is not None:
        return int(marks_default)
    return DEFAULT_MAX_WIDTH


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.marks:
        marks_path = Path(args.marks)
        marks = load_marks(marks_path)
        out_dir = Path(args.out_dir or (marks_path.parent / "clips"))
        max_w = resolve_max_width(args, marks.get("max_width"))
        source = marks["source"]
        for entry in marks["clips"]:
            name, start, end = clip_window(entry)
            safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)
            export_one(
                source=source,
                out_path=out_dir / f"{safe}.mp4",
                start_sec=start,
                end_sec=end,
                max_width=max_w,
                crf=args.crf,
                preset=args.x264_preset,
                audio=not args.no_audio,
                dry_run=args.dry_run,
            )
        return 0

    if not args.input or not args.output:
        build_parser().error("single-clip mode needs -i/--input and -o/--output (or use --marks)")

    if args.start_sec is not None:
        start = float(args.start_sec)
    elif args.start is not None:
        start = parse_time_to_seconds(args.start)
    else:
        start = 0.0

    end = None
    duration = args.duration
    if args.end_sec is not None:
        end = float(args.end_sec)
    elif args.end is not None:
        end = parse_time_to_seconds(args.end)

    max_w = resolve_max_width(args)
    export_one(
        source=args.input,
        out_path=Path(args.output),
        start_sec=start,
        end_sec=end if end is not None else start + (duration or 0),
        max_width=max_w,
        crf=args.crf,
        preset=args.x264_preset,
        audio=not args.no_audio,
        dry_run=args.dry_run,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
