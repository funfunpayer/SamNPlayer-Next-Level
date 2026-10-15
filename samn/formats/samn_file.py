"""The ``.samn`` script container (port of SamNPlayer Go package ``samn``).

``.samn`` is SamNPlayer's own script file: a JSON document with the general
stroke curve, optional vibration/suction curves, device recipe, chapters,
strength presets, ...  This module reads and writes exactly the files the Go
app does:

* JSON keys, key order (Go struct order) and ``omitempty`` rules are the same;
  floats are rendered the way Go's ``encoding/json`` renders them, strings are
  escaped the same way (``<``, ``>``, ``&``, U+2028/9), indent is two spaces,
  there is no trailing newline.
* Unknown top-level keys of a ``.samn`` are dropped on read, exactly like the
  Go ``json.Unmarshal`` into ``Document`` does (Go does not preserve them).
  ``export_funscript`` however keeps foreign keys of an existing target file,
  as Go does (raw number text is kept verbatim there).
* Writes are atomic (temp file in the same folder + ``os.replace``).

Actions are lists of ``(at_ms, pos)`` int tuples.  Go names are given in each
docstring.  Not modelled: ``ai_opinion`` / ``audio_check`` of the typed Go
funscript metadata (they are only ever *preserved*, never interpreted, here).
"""
from __future__ import annotations

import copy
import dataclasses
import json
import math
import os
import tempfile
import types
from dataclasses import dataclass, field
from decimal import Decimal
from typing import Any, Union, get_args, get_origin, get_type_hints

from .mapper import (MapOptions, SceneFeelRange, SyncMode, TrackingGap,
                     clamp01, default_map_options, effective_contact_span,
                     normalize_contact_curve, parse_sync_mode,
                     playback_actions, to_intensity_curve)

Point = tuple[int, int]  # Go: Point (= funscript.Action): (at_ms, pos 0-100)

KIND = "samnplayer.script"
CURRENT_VERSION = 1
PLAYBACK_RECIPE = "recipe"
PLAYBACK_AXES = "axes"
EXT = ".samn"
MAX_ACTION_MS = 48 * 3600 * 1000  # Go: funscript.MaxActionMs

PROFILE_STANDARD = "standard"
PROFILE_WEICH = "weich"
PROFILE_TF = "tf"
PROFILE_TJ = "tj"

O_MARKER_PRIMARY = "primary"
O_MARKER_SECONDARY = "secondary"


class SamnError(ValueError):
    """Any error the Go package reports as ``samn: ...``."""


# ---------------------------------------------------------------------------
# Data model (field order == Go struct order == JSON key order)
# ---------------------------------------------------------------------------

def _j(name: str, *, omit: bool = False, dec: str = "", default: Any = dataclasses.MISSING,
       factory: Any = dataclasses.MISSING) -> Any:
    """dataclass field with JSON name / omitempty / special decoder metadata."""
    md = {"json": name, "omit": omit, "dec": dec}
    if factory is not dataclasses.MISSING:
        return field(default_factory=factory, metadata=md)
    if default is not dataclasses.MISSING:
        return field(default=default, metadata=md)
    return field(metadata=md)


@dataclass
class StrengthPreset:
    """Go: StrengthPreset."""
    name: str = _j("name", default="")
    vibration_scale: float = _j("vibrationScale", default=0.0)
    suction_scale: float = _j("suctionScale", default=0.0)
    contact_span: float = _j("contactSpan", omit=True, default=0.0)
    contact_curve: str = _j("contactCurve", omit=True, default="")


@dataclass
class DeviceRecipe:
    """Go: funscript.DeviceRecipe."""
    sync: str = _j("sync", default="")
    min_suction: float = _j("min_suction", default=0.0)
    tick_ms: int = _j("tick_ms", default=0)
    max_speed: float = _j("max_speed", default=0.0)
    smoothing: float = _j("smoothing", default=0.0)
    contact_vibration: bool = _j("contact_vibration", omit=True, default=False)
    contact_vibration_span: float = _j("contact_vibration_span", omit=True, default=0.0)
    contact_vibration_curve: str = _j("contact_vibration_curve", omit=True, default="")
    scene_feel: str = _j("scene_feel", omit=True, default="")
    vibration_follows_suction: bool = _j("vibration_follows_suction", omit=True, default=False)
    feel_balance: float = _j("feel_balance", omit=True, default=0.0)
    scene_ranges: list[SceneFeelRange] = _j("scene_ranges", omit=True, factory=list)
    playback_source: str = _j("playback_source", omit=True, default="")


@dataclass
class ChapterMark:
    """Go: funscript.ChapterMark."""
    name: str = _j("name", default="")
    start_time: int = _j("startTime", default=0)
    end_time: int = _j("endTime", omit=True, default=0)


@dataclass
class Bookmark:
    """Go: funscript.Bookmark."""
    name: str = _j("name", default="")
    time: int = _j("time", default=0)


@dataclass
class OMarker:
    """Go: funscript.OMarker."""
    start_ms: int = _j("startMs", default=0)
    end_ms: int = _j("endMs", default=0)
    kind: str = _j("kind", default="")
    intensity: float = _j("intensity", default=0.0)

    def validate(self) -> None:
        """Go: OMarker.Validate. Raises SamnError."""
        if self.end_ms <= self.start_ms:
            raise SamnError(f"funscript: O-Marker Ende ({self.end_ms}ms) muss nach dem Start "
                            f"({self.start_ms}ms) liegen")
        if self.kind not in (O_MARKER_PRIMARY, O_MARKER_SECONDARY):
            raise SamnError(f"funscript: unbekannte O-Marker-Art {_go_quote(self.kind)} "
                            f'(erwartet "{O_MARKER_PRIMARY}" oder "{O_MARKER_SECONDARY}")')
        if self.intensity < 0 or self.intensity > 1:
            raise SamnError(f"funscript: O-Marker-Intensität {_go_float(self.intensity)} "
                            "außerhalb von 0..1")


@dataclass
class ContactMarkBox:
    """Go: funscript.ContactMarkBox."""
    x: int = _j("x", default=0)
    y: int = _j("y", default=0)
    w: int = _j("w", default=0)
    h: int = _j("h", default=0)
    cls: str = _j("class", omit=True, default="")
    fixed: bool = _j("fixed", omit=True, default=False)


@dataclass
class ContactMarks:
    """Go: funscript.ContactMarks."""
    tip_class: str = _j("tip_class", omit=True, default="")
    tip: ContactMarkBox | None = _j("tip", omit=True, default=None)
    primary: ContactMarkBox | None = _j("primary", omit=True, default=None)
    extras: list[ContactMarkBox] = _j("extras", omit=True, factory=list)
    drive_stroke: bool = _j("drive_stroke", default=False)


@dataclass
class TrajectoryPoint:
    """Go: funscript.TrajectoryPoint."""
    at_ms: int = _j("atMs", default=0)
    x: float = _j("x", default=0.0)
    y: float = _j("y", default=0.0)


@dataclass
class TrajectoryData:
    """Go: funscript.TrajectoryData."""
    width: int = _j("width", default=0)
    height: int = _j("height", default=0)
    tip: list[TrajectoryPoint] = _j("tip", omit=True, factory=list)
    partner: list[TrajectoryPoint] = _j("partner", omit=True, factory=list)


@dataclass
class SceneMapVideo:
    """Go: funscript.SceneMapVideo."""
    duration_ms: int = _j("durationMs", default=0)
    width: int = _j("width", default=0)
    height: int = _j("height", default=0)
    sha256_head: str = _j("sha256_head", omit=True, default="")


@dataclass
class SceneMapGrid:
    """Go: funscript.SceneMapGrid."""
    cols: int = _j("cols", default=0)
    rows: int = _j("rows", default=0)


@dataclass
class SceneMapWindow:
    """Go: funscript.SceneMapWindow."""
    start_ms: int = _j("startMs", default=0)
    end_ms: int = _j("endMs", default=0)
    tempo_hz: float = _j("tempoHz", default=0.0)
    score_b64: str = _j("score_b64", default="")
    chosen: int = _j("chosen", default=0)
    sign_rule: str = _j("signRule", omit=True, default="")
    tracker_r: float = _j("trackerR", omit=True, default=0.0)
    box: list[float] = _j("box", omit=True, factory=list)
    marks: list[str] = _j("marks", omit=True, factory=list)


@dataclass
class SceneMapMarkSample:
    """Go: funscript.SceneMapMarkSample."""
    ms: int = _j("ms", default=0)
    rect: list[int] = _j("rect", factory=list)


@dataclass
class SceneMapMark:
    """Go: funscript.SceneMapMark."""
    id: str = _j("id", default="")
    kind: str = _j("kind", default="")
    rect: list[int] = _j("rect", factory=list)
    from_ms: int = _j("fromMs", omit=True, default=0)
    to_ms: int = _j("toMs", omit=True, default=0)
    at_ms: int | None = _j("atMs", omit=True, default=None)
    author: str = _j("author", omit=True, default="")
    created_at: str = _j("createdAt", omit=True, default="")
    cls: str = _j("class", omit=True, default="")
    role: str = _j("role", omit=True, default="")
    confidence: float = _j("confidence", omit=True, default=0.0)
    reviewed: bool | None = _j("reviewed", omit=True, default=None)
    follow: bool = _j("follow", omit=True, default=False)
    path: list[SceneMapMarkSample] = _j("path", omit=True, factory=list)


@dataclass
class SceneMapEvent:
    """Go: funscript.SceneMapEvent."""
    at_ms: int = _j("atMs", default=0)
    kind: str = _j("kind", default="")
    from_box: list[int] = _j("fromBox", omit=True, factory=list)
    to_box: list[int] = _j("toBox", omit=True, factory=list)


@dataclass
class SceneMapData:
    """Go: funscript.SceneMapData."""
    version: int = _j("version", default=0)
    video: SceneMapVideo = _j("video", factory=SceneMapVideo)
    grid: SceneMapGrid = _j("grid", factory=SceneMapGrid)
    windows: list[SceneMapWindow] = _j("windows", omit=True, factory=list)
    marks: list[SceneMapMark] = _j("marks", omit=True, factory=list)
    events: list[SceneMapEvent] = _j("events", omit=True, factory=list)


@dataclass
class Document:
    """Go: Document (the native .samn script file)."""
    version: int = _j("version", default=0)
    kind: str = _j("kind", default="")

    creator: str = _j("creator", omit=True, default="")
    duration_ms: int = _j("durationMs", omit=True, default=0)
    video_path: str = _j("videoPath", omit=True, default="")
    profile: str = _j("profile", omit=True, default="")

    playback_source: str = _j("playbackSource", omit=True, default="")
    recipe: DeviceRecipe = _j("recipe", factory=DeviceRecipe)  # struct: omitempty has no effect

    general: list[Point] = _j("general", dec="actions", factory=list)
    vibration: list[Point] = _j("vibration", omit=True, dec="actions", factory=list)
    suction: list[Point] = _j("suction", omit=True, dec="actions", factory=list)

    chapters: list[ChapterMark] = _j("chapters", omit=True, factory=list)
    bookmarks: list[Bookmark] = _j("bookmarks", omit=True, factory=list)
    o_markers: list[OMarker] = _j("oMarkers", omit=True, factory=list)
    tracking_gaps: list[TrackingGap] = _j("trackingGaps", omit=True, factory=list)

    strength_presets: list[StrengthPreset] = _j("strengthPresets", omit=True, factory=list)
    active_strength: str = _j("activeStrength", omit=True, default="")

    contact_marks: ContactMarks | None = _j("contactMarks", omit=True, default=None)
    trajectory: TrajectoryData | None = _j("trajectory", omit=True, default=None)
    scene_map: SceneMapData | None = _j("sceneMap", omit=True, default=None)

    quality_score: float | None = _j("qualityScore", omit=True, default=None)
    quality_passed: bool | None = _j("qualityPassed", omit=True, default=None)
    quality_warnings: list[str] = _j("qualityWarnings", omit=True, factory=list)

    # -- methods defined below (normalize, bake_neo_axes, ...) ---------------


# Typed view of a community .funscript (Go: funscript.Script, the parts samn uses)

@dataclass
class SamnAxes:
    """Go: funscript.SamnAxes."""
    vibration: list[Point] = _j("vibration", omit=True, dec="actions", factory=list)
    suction: list[Point] = _j("suction", omit=True, dec="actions", factory=list)


@dataclass
class ScriptMetadata:
    """Go: funscript.Script.Metadata (without ai_opinion / audio_check)."""
    duration: int = _j("duration", dec="metaduration", default=0)
    creator: str = _j("creator", default="")
    quality_score: float | None = _j("quality_score", omit=True, default=None)
    quality_passed: bool | None = _j("quality_passed", omit=True, default=None)
    quality_warnings: list[str] = _j("quality_warnings", omit=True, factory=list)
    profile: str = _j("profile", omit=True, default="")
    device_recipe: DeviceRecipe | None = _j("device_recipe", omit=True, default=None)
    samn_axes: SamnAxes | None = _j("samn_axes", omit=True, default=None)
    tracking_gaps: list[TrackingGap] = _j("tracking_gaps", omit=True, factory=list)
    trajectory: TrajectoryData | None = _j("trajectory", omit=True, default=None)
    contact_marks: ContactMarks | None = _j("contact_marks", omit=True, default=None)


@dataclass
class Script:
    """Go: funscript.Script."""
    actions: list[Point] = _j("actions", dec="actions", factory=list)
    inverted: bool = _j("inverted", omit=True, default=False)
    dropped: int = field(default=0, metadata={"json": "-"})
    metadata: ScriptMetadata = _j("metadata", omit=True, factory=ScriptMetadata)

    def duration(self) -> int:
        """Go: Script.Duration."""
        return self.actions[-1][0] if self.actions else 0

    def playback_actions(self) -> list[Point]:
        """Go: Script.PlaybackActions (100-pos when inverted)."""
        return playback_actions(self.actions, self.inverted)


@dataclass
class _ChapterWire:
    name: str = _j("name", default="")
    start_time: int = _j("startTime", dec="flexms", default=0)
    end_time: int = _j("endTime", dec="flexms", default=0)


@dataclass
class _BookmarkWire:
    name: str = _j("name", default="")
    time: int = _j("time", dec="flexms", default=0)


# ---------------------------------------------------------------------------
# Go-compatible JSON number / string rendering
# ---------------------------------------------------------------------------

class _RawNum(float):
    """A float that remembers its source text (Go json.RawMessage fidelity)."""
    text: str

    def __new__(cls, text: str) -> "_RawNum":
        obj = super().__new__(cls, float(text))
        obj.text = text
        return obj


def _go_float(f: float) -> str:
    """Render a float like Go's encoding/json (shortest, 'f' unless huge/tiny)."""
    if math.isnan(f) or math.isinf(f):
        raise SamnError(f"json: unsupported value: {f}")
    if f == 0:
        return "-0" if math.copysign(1.0, f) < 0 else "0"
    a = abs(f)
    d = Decimal(repr(a))
    if a < 1e-6 or a >= 1e21:
        sign, digits, exp = d.as_tuple()
        ds = "".join(map(str, digits)).rstrip("0") or "0"
        e10 = exp + len("".join(map(str, digits))) - 1
        mant = ds[0] + ("." + ds[1:] if len(ds) > 1 else "")
        es = f"e+{e10:02d}" if e10 >= 0 else f"e-{-e10}"
        out = mant + es
    else:
        out = format(d, "f")
        if "." in out:
            out = out.rstrip("0").rstrip(".")
    return "-" + out if f < 0 else out


def _go_quote(s: str) -> str:
    """Go %q for the plain cases used in messages."""
    return json.dumps(s, ensure_ascii=False)


_ESC = {'"': '\\"', "\\": "\\\\", "\n": "\\n", "\r": "\\r", "\t": "\\t",
        "\b": "\\b", "\f": "\\f", "<": "\\u003c", ">": "\\u003e", "&": "\\u0026",
        " ": "\\u2028", " ": "\\u2029"}


def _go_string(s: str) -> str:
    out = ['"']
    for ch in s:
        e = _ESC.get(ch)
        if e is not None:
            out.append(e)
        elif ch < " ":
            out.append(f"\\u{ord(ch):04x}")
        elif "\ud800" <= ch <= "\udfff":
            out.append("�")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def _dump(v: Any, level: int = 0) -> str:
    """json.MarshalIndent(v, "", "  ") for the plain values built by _enc."""
    if v is None:
        return "null"
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, _RawNum):
        return v.text
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return _go_float(v)
    if isinstance(v, str):
        return _go_string(v)
    pad = "  " * (level + 1)
    end = "  " * level
    if isinstance(v, dict):
        if not v:
            return "{}"
        items = [f"{pad}{_go_string(k)}: {_dump(x, level + 1)}" for k, x in v.items()]
        return "{\n" + ",\n".join(items) + "\n" + end + "}"
    if isinstance(v, (list, tuple)):
        if not v:
            return "[]"
        return "[\n" + ",\n".join(pad + _dump(x, level + 1) for x in v) + "\n" + end + "]"
    raise TypeError(f"cannot encode {type(v).__name__}")


# ---------------------------------------------------------------------------
# Generic dataclass <-> JSON mapping (driven by field metadata / type hints)
# ---------------------------------------------------------------------------

_HINTS: dict[type, dict[str, Any]] = {}


def _hints(cls: type) -> dict[str, Any]:
    h = _HINTS.get(cls)
    if h is None:
        h = _HINTS[cls] = get_type_hints(cls)
    return h


def _is_union(tp: Any) -> bool:
    return get_origin(tp) in (Union, types.UnionType)


def _is_ptr(tp: Any) -> bool:
    return _is_union(tp) and type(None) in get_args(tp)


def _unptr(tp: Any) -> Any:
    return next(a for a in get_args(tp) if a is not type(None))


def _json_name(f: dataclasses.Field) -> str:
    return f.metadata.get("json", f.name)


def _empty(v: Any, ptr: bool) -> bool:
    if ptr:
        return v is None
    if v is None:
        return True
    if isinstance(v, (list, dict, str)):
        return len(v) == 0
    if isinstance(v, (bool, int, float)):
        return v == 0
    return False  # struct: omitempty never omits


def _enc(v: Any) -> Any:
    """Dataclass tree -> plain dict/list tree in Go struct order."""
    if dataclasses.is_dataclass(v) and not isinstance(v, type):
        hints = _hints(type(v))
        out: dict[str, Any] = {}
        for f in dataclasses.fields(v):
            name = _json_name(f)
            if name == "-":
                continue
            val = getattr(v, f.name)
            if f.metadata.get("omit") and _empty(val, _is_ptr(hints[f.name])):
                continue
            out[name] = _enc(val)
        return out
    if isinstance(v, tuple):  # an action
        return {"at": int(v[0]), "pos": int(v[1])}
    if isinstance(v, list):
        return [_enc(x) for x in v]
    if isinstance(v, dict):
        return {k: _enc(x) for k, x in v.items()}
    return v


def _go_round(f: float) -> int:
    """Go math.Round (half away from zero)."""
    return int(math.floor(abs(f) + 0.5)) * (1 if f >= 0 else -1)


def _round_number(v: Any, what: str) -> int:
    """Go: roundNumber on a json.Number (null -> 0, non-numbers fail)."""
    if v is None:
        return 0
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise SamnError(f"{what}: not a number")
    if isinstance(v, int):
        if -(1 << 63) <= v < (1 << 63):
            return v
        v = float(v)
    return _go_round(max(-1e15, min(1e15, float(v))))


def _action_from_json(v: Any) -> Point:
    """Go: Action.UnmarshalJSON (accepts float at/pos, rounds)."""
    if v is None:
        return (0, 0)
    if not isinstance(v, dict):
        raise SamnError("cannot unmarshal into Action")
    lk = {k.lower(): x for k, x in v.items()}
    at = _round_number(v.get("at", lk.get("at")), "at")
    pos = _round_number(v.get("pos", lk.get("pos")), "pos")
    return (at, pos)


def _normalize_time_to_ms(v: float) -> int:
    if math.isnan(v) or math.isinf(v):
        return 0
    a = abs(v)
    frac = a - math.floor(a)
    if a < 100000 and frac > 1e-9:
        return _go_round(v * 1000)
    return _go_round(v)


def _parse_clock_ms(s: str) -> int | None:
    parts = s.strip().split(":")
    if len(parts) < 2 or len(parts) > 3:
        return None
    total = 0.0
    for i, p in enumerate(parts):
        try:
            x = float(p)
        except ValueError:
            return None
        if x < 0 or math.isnan(x) or math.isinf(x):
            return None
        if i < len(parts) - 1 and x != math.trunc(x):
            return None
        total = total * 60 + x
    return _go_round(total * 1000)


def _flex_ms(v: Any) -> int:
    """Go: flexTimeMs.UnmarshalJSON."""
    if v is None:
        return 0
    if isinstance(v, bool):
        raise SamnError("cannot unmarshal bool into flexTimeMs")
    if isinstance(v, str):
        try:
            return _normalize_time_to_ms(float(v))
        except ValueError:
            ms = _parse_clock_ms(v)
            if ms is None:
                raise SamnError(f"invalid time {v!r}") from None
            return ms
    if isinstance(v, (int, float)):
        return _normalize_time_to_ms(float(v))
    raise SamnError("cannot unmarshal value into flexTimeMs")


def _zero(tp: Any) -> Any:
    if _is_ptr(tp):
        return None
    org = get_origin(tp)
    if org is list:
        return []
    if tp is int:
        return 0
    if tp is float:
        return 0.0
    if tp is str:
        return ""
    if tp is bool:
        return False
    if dataclasses.is_dataclass(tp):
        return _build(tp, {})
    return None


def _dec(tp: Any, v: Any, dec: str = "") -> Any:
    if dec == "actions":
        if v is None:
            return []
        if not isinstance(v, list):
            raise SamnError("cannot unmarshal non-array into actions")
        return [_action_from_json(x) for x in v]
    if dec == "flexms":
        return _flex_ms(v)
    if dec == "metaduration":
        try:
            return _flex_ms(v)
        except SamnError:
            return 0
    if _is_ptr(tp):
        return None if v is None else _dec(_unptr(tp), v)
    if v is None:
        return _zero(tp)
    org = get_origin(tp)
    if org is list:
        if not isinstance(v, list):
            raise SamnError(f"cannot unmarshal {type(v).__name__} into list")
        (et,) = get_args(tp)
        return [_dec(et, x) for x in v]
    if tp is bool:
        if not isinstance(v, bool):
            raise SamnError("cannot unmarshal non-bool into bool")
        return v
    if tp is int:
        if isinstance(v, bool) or not isinstance(v, int) or isinstance(v, _RawNum):
            raise SamnError(f"cannot unmarshal {v!r} into integer")
        return v
    if tp is float:
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise SamnError("cannot unmarshal non-number into float")
        return float(v)
    if tp is str:
        if not isinstance(v, str):
            raise SamnError("cannot unmarshal non-string into string")
        return v
    if dataclasses.is_dataclass(tp):
        if not isinstance(v, dict):
            raise SamnError(f"cannot unmarshal {type(v).__name__} into {tp.__name__}")
        return _build(tp, v)
    raise TypeError(f"unsupported type {tp}")


def _build(cls: type, obj: dict[str, Any]) -> Any:
    hints = _hints(cls)
    lower = {k.lower(): k for k in obj}  # Go matches keys case-insensitively
    kwargs: dict[str, Any] = {}
    for f in dataclasses.fields(cls):
        name = _json_name(f)
        has_default = (f.default is not dataclasses.MISSING
                       or f.default_factory is not dataclasses.MISSING)
        if name == "-":
            continue
        key = name if name in obj else lower.get(name.lower())
        if key is None:
            if not has_default:
                kwargs[f.name] = _zero(hints[f.name])
            continue
        kwargs[f.name] = _dec(hints[f.name], obj[key], f.metadata.get("dec", ""))
    return cls(**kwargs)


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

def _go_ext(path: str) -> str:
    """Go: filepath.Ext (from the last '.' of the final element)."""
    for i in range(len(path) - 1, -1, -1):
        if path[i] == "/":
            break
        if path[i] == ".":
            return path[i:]
    return ""


def is_samn_path(path: str | os.PathLike[str]) -> bool:
    """Go: IsSamnPath."""
    return _go_ext(os.fspath(path)).casefold() == EXT


def companion_samn_path(path: str | os.PathLike[str]) -> str:
    """Go: CompanionSamnPath."""
    p = os.fspath(path)
    return p[: len(p) - len(_go_ext(p))] + EXT


def companion_funscript_path(path: str | os.PathLike[str]) -> str:
    """Go: CompanionFunscriptPath."""
    p = os.fspath(path)
    return p[: len(p) - len(_go_ext(p))] + ".funscript"


def prefer_samn_companion(path: str | os.PathLike[str]) -> str:
    """Go: preferSamnCompanion (cmd/gui-wails/app_review.go).

    Returns the companion .samn when it exists (as a file) beside a
    .funscript; empty paths and .samn paths come back unchanged.
    """
    p = os.fspath(path) if path else ""
    if p == "" or is_samn_path(p):
        return p
    companion = companion_samn_path(p)
    if os.path.isfile(companion):
        return companion
    return p


# ---------------------------------------------------------------------------
# Normalize / validation
# ---------------------------------------------------------------------------

def normalize_playback_source(s: str) -> str:
    """Go: funscript.NormalizePlaybackSource."""
    return PLAYBACK_AXES if s.strip().lower() == PLAYBACK_AXES else PLAYBACK_RECIPE


def normalize_profile(name: str) -> str:
    """Go: funscript.NormalizeProfile."""
    n = name.strip().lower()
    if n in ("tf", "tj"):
        return PROFILE_TJ
    if n == "weich":
        return PROFILE_WEICH
    if n in ("", "standard"):
        return PROFILE_STANDARD
    return n


def is_distance_profile(name: str) -> bool:
    """Go: funscript.IsDistanceProfile."""
    return normalize_profile(name) in (PROFILE_TJ, PROFILE_TF)


def sanitize_actions(actions: list[Point]) -> list[Point]:
    """Go: funscript.SanitizeActions (clamp pos 0-100, reject negative time, sort)."""
    if not actions:
        raise SamnError("funscript: mindestens ein Punkt wird benötigt")
    out: list[Point] = []
    for i, (at, pos) in enumerate(actions):
        if at < 0:
            raise SamnError(f"funscript: Punkt {i} hat eine negative Zeit ({at}ms)")
        out.append((at, max(0, min(100, pos))))
    out.sort(key=lambda a: a[0])  # stable, like sort.Slice for distinct times
    return out


def _normalize_scene_feel(name: str) -> str:
    n = name.strip().lower()
    if n in ("oral", "blowjob", "bj", "mouth", "lips"):
        return "oral"
    if n in ("intercourse", "penetration", "sex"):
        return "intercourse"
    if n in ("breast", "titjob", "tj", "boobjob", "nipple", "nipples"):
        return "breast"
    if n in ("hand", "handjob", "hj"):
        return "hand"
    return ""


def _normalize_scene_ranges(rs: list[SceneFeelRange]) -> list[SceneFeelRange]:
    return [SceneFeelRange(r.start_ms, r.end_ms, _normalize_scene_feel(r.feel))
            for r in rs if r.end_ms > r.start_ms]


def _clamp_balance(b: float) -> float:
    if b != b:
        return 0.0
    return max(-1.0, min(1.0, b))


def _normalize(d: Document) -> None:
    if d.version == 0:
        d.version = CURRENT_VERSION
    if d.version != CURRENT_VERSION:
        raise SamnError(f"samn: unsupported version {d.version} (want {CURRENT_VERSION})")
    if d.kind == "":
        d.kind = KIND
    if d.kind != KIND:
        raise SamnError(f"samn: unexpected kind {_go_quote(d.kind)}")
    if len(d.general) == 0:
        raise SamnError("samn: general curve is required")
    d.general = sanitize_actions(d.general)
    if d.vibration:
        d.vibration = sanitize_actions(d.vibration)
    if d.suction:
        d.suction = sanitize_actions(d.suction)
    d.playback_source = normalize_playback_source(d.playback_source)
    d.profile = normalize_profile(d.profile)
    if d.duration_ms <= 0:
        d.duration_ms = d.general[-1][0]
    for i, m in enumerate(d.o_markers):
        try:
            m.validate()
        except SamnError as e:
            raise SamnError(f"samn: oMarkers[{i}]: {e}") from None


def _doc_normalize(self: Document) -> None:
    """Go: Document.Normalize. Fills defaults and clamps curves; raises SamnError."""
    _normalize(self)


Document.normalize = _doc_normalize  # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# Atomic write
# ---------------------------------------------------------------------------

def _write_atomic(path: str | os.PathLike[str], data: bytes) -> None:
    """Temp file in the same folder + fsync + os.replace (Go: writeFileAtomic)."""
    target = os.fspath(path)
    folder = os.path.dirname(target) or "."
    fd, tmp = tempfile.mkstemp(prefix="." + os.path.basename(target) + ".tmp-", dir=folder)
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        umask = os.umask(0)
        os.umask(umask)
        os.chmod(tmp, 0o644 & ~umask)
        os.replace(tmp, target)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# ---------------------------------------------------------------------------
# I/O: Load / Parse / Save
# ---------------------------------------------------------------------------

def _no_const(name: str) -> Any:
    raise ValueError(f"invalid number {name}")


def _loads(data: bytes | str, raw_numbers: bool = False) -> Any:
    text = data.decode("utf-8", errors="replace") if isinstance(data, (bytes, bytearray)) else data
    return json.loads(text, parse_float=_RawNum if raw_numbers else float,
                      parse_constant=_no_const)


def parse(data: bytes | str) -> Document:
    """Go: Parse (unmarshal JSON bytes, then Normalize). Unknown keys are dropped."""
    try:
        obj = _loads(data)
    except (ValueError, RecursionError) as e:
        raise SamnError(f"samn: invalid JSON: {e}") from None
    if obj is None:
        d = Document()
    elif isinstance(obj, dict):
        try:
            d = _build(Document, obj)
        except SamnError as e:
            raise SamnError(f"samn: invalid JSON: {e}") from None
    else:
        raise SamnError("samn: invalid JSON: cannot unmarshal "
                        f"{type(obj).__name__} into Go value of type samn.Document")
    d.normalize()
    return d


def load(path: str | os.PathLike[str]) -> Document:
    """Go: Load (read and validate a .samn document)."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as e:
        raise SamnError(f"samn: read: {e}") from None
    return parse(data)


def dumps(d: Document) -> bytes:
    """Go: json.MarshalIndent(d, "", "  ") of an already normalized Document."""
    return _dump(_enc(d)).encode("utf-8")


def save(path: str | os.PathLike[str], d: Document | None) -> None:
    """Go: Save (Normalize, then write indented JSON atomically)."""
    if d is None:
        raise SamnError("samn: nil document")
    d.normalize()
    _write_atomic(path, dumps(d))


# ---------------------------------------------------------------------------
# funscript side (Go: funscript.Parse / Load, used by convert.go)
# ---------------------------------------------------------------------------

def parse_funscript(data: bytes | str) -> Script:
    """Go: funscript.Parse (drops corrupt times, clamps pos, sorts, scales seconds)."""
    try:
        obj = _loads(data)
        if not isinstance(obj, dict):
            raise SamnError("cannot unmarshal into Script")
        s: Script = _build(Script, obj)
    except (ValueError, RecursionError) as e:
        raise SamnError(f"funscript: ungültiges JSON: {e}") from None
    if not s.actions:
        raise SamnError("funscript: keine actions im Skript gefunden")
    kept = [a for a in s.actions if 0 <= a[0] <= MAX_ACTION_MS]
    s.dropped = len(s.actions) - len(kept)
    if s.dropped > 0:
        s.actions = kept
        if not s.actions:
            raise SamnError("funscript: keine gültigen actions (alle Zeiten außerhalb "
                            f"0–{MAX_ACTION_MS} ms)")
    s.actions = [(at, max(0, min(100, pos))) for at, pos in s.actions]
    s.actions.sort(key=lambda a: a[0])
    d, last = s.metadata.duration, s.duration()
    if d > 0 and d * 100 < last:
        s.metadata.duration = d * 1000
    return s


def load_funscript(path: str | os.PathLike[str]) -> Script:
    """Go: funscript.Load."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as e:
        raise SamnError(f"funscript: Datei konnte nicht gelesen werden: {e}") from None
    return parse_funscript(data)


def _read_metadata_map(path: str | os.PathLike[str]) -> dict[str, Any]:
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as e:
        raise SamnError(f"funscript: Datei konnte nicht gelesen werden: {e}") from None
    try:
        doc = _loads(data)
    except (ValueError, RecursionError) as e:
        raise SamnError(f"funscript: ungültiges JSON: {e}") from None
    if not isinstance(doc, dict):
        raise SamnError("funscript: ungültiges JSON: not an object")
    meta = doc.get("metadata")
    if meta is None:
        return {}
    if not isinstance(meta, dict):
        raise SamnError("funscript: metadata ungültig")
    return meta


def _load_wire_list(path: str | os.PathLike[str], key: str, cls: type) -> list[Any]:
    raw = _read_metadata_map(path).get(key)
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise SamnError(f"funscript: {key} ungültig")
    return [_dec(cls, x) for x in raw]


def load_chapters(path: str | os.PathLike[str]) -> list[ChapterMark]:
    """Go: funscript.LoadChapters (OFS-style times: seconds, clock strings)."""
    return [ChapterMark(w.name, w.start_time, w.end_time)
            for w in _load_wire_list(path, "chapters", _ChapterWire)]


def load_bookmarks(path: str | os.PathLike[str]) -> list[Bookmark]:
    """Go: funscript.LoadBookmarks."""
    return [Bookmark(w.name, w.time) for w in _load_wire_list(path, "bookmarks", _BookmarkWire)]


def load_o_markers(path: str | os.PathLike[str]) -> list[OMarker]:
    """Go: funscript.LoadOMarkers."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as e:
        raise SamnError(f"funscript: Datei konnte nicht gelesen werden: {e}") from None
    try:
        doc = _loads(data)
        if not isinstance(doc, dict):
            raise SamnError("not an object")
        meta = doc.get("metadata") or {}
        if not isinstance(meta, dict):
            raise SamnError("metadata ungültig")
        raw = meta.get("oMarkers")
        if raw is None:
            return []
        return _dec(list[OMarker], raw)
    except (ValueError, RecursionError) as e:
        raise SamnError(f"funscript: ungültiges JSON: {e}") from None


# ---------------------------------------------------------------------------
# Recipe helpers (Go: funscript.RecipeFor / RecipeMeta / MapOptionsFromScript)
# ---------------------------------------------------------------------------

def recipe_for(name: str) -> MapOptions:
    """Go: funscript.RecipeFor."""
    opts = default_map_options()
    n = normalize_profile(name)
    if n in (PROFILE_TJ, PROFILE_TF):
        opts.sync = SyncMode.SUCTION_POSITION
        opts.min_vibration = 0
        opts.min_suction = 0.20
        opts.tick_ms = 50
        opts.max_speed = 0.50
        opts.smoothing = 0.22
    elif n == PROFILE_WEICH:
        opts.sync = SyncMode.INDEPENDENT
        opts.min_vibration = 0.12
        opts.min_suction = 0.10
        opts.smoothing = 0.28
    return opts


def recipe_meta(name: str) -> DeviceRecipe:
    """Go: funscript.RecipeMeta."""
    o = recipe_for(name)
    return DeviceRecipe(sync=str(o.sync), min_suction=o.min_suction, tick_ms=o.tick_ms,
                        max_speed=o.max_speed, smoothing=o.smoothing)


def default_sync_for_profile(profile: str) -> str:
    """Go: funscript.DefaultSyncForProfile."""
    return str(SyncMode.SUCTION_POSITION if is_distance_profile(profile) else SyncMode.INDEPENDENT)


def map_options_from_script(s: Script | None) -> MapOptions:
    """Go: funscript.MapOptionsFromScript (recipe + explicit axes)."""
    opts = default_map_options()
    if s is None:
        return opts
    if s.metadata.profile != "":
        opts = recipe_for(s.metadata.profile)
    dr = s.metadata.device_recipe
    if dr is not None:
        if dr.sync != "":
            try:
                opts.sync = parse_sync_mode(dr.sync)
            except ValueError:
                pass
        if dr.tick_ms > 0:
            opts.tick_ms = dr.tick_ms
        if dr.max_speed > 0:
            opts.max_speed = dr.max_speed
        if dr.smoothing > 0:
            opts.smoothing = dr.smoothing
        if dr.min_suction > 0:
            opts.min_suction = dr.min_suction
        opts.contact_vibration = dr.contact_vibration
        opts.contact_vibration_span = dr.contact_vibration_span
        opts.contact_vibration_curve = dr.contact_vibration_curve
        opts.scene_feel = _normalize_scene_feel(dr.scene_feel)
        opts.scene_ranges = _normalize_scene_ranges(dr.scene_ranges)
        opts.vibration_follows_suction = dr.vibration_follows_suction
        opts.balance = dr.feel_balance
        if (normalize_playback_source(dr.playback_source) == PLAYBACK_AXES
                and s.metadata.samn_axes is not None):
            opts.use_explicit_axes = True
            opts.vibration_axis = list(s.metadata.samn_axes.vibration)
            opts.suction_axis = list(s.metadata.samn_axes.suction)
    if s.metadata.tracking_gaps:
        opts.tracking_gaps = list(s.metadata.tracking_gaps)
    return opts


# ---------------------------------------------------------------------------
# convert.go
# ---------------------------------------------------------------------------

def from_funscript(s: Script, video_path: str = "") -> Document:
    """Go: FromFunscript (community/legacy .funscript -> Document, as played)."""
    m = s.metadata
    d = Document(
        version=CURRENT_VERSION, kind=KIND, video_path=video_path, creator=m.creator,
        duration_ms=int(m.duration), profile=m.profile,
        general=list(s.playback_actions()),  # an OFS "inverted" flag is baked in
    )
    if d.duration_ms <= 0:
        d.duration_ms = s.duration()
    if m.device_recipe is not None:
        d.recipe = copy.deepcopy(m.device_recipe)
        d.playback_source = normalize_playback_source(m.device_recipe.playback_source)
    else:
        d.playback_source = PLAYBACK_RECIPE
    if m.samn_axes is not None:
        d.vibration = list(m.samn_axes.vibration)
        d.suction = list(m.samn_axes.suction)
    d.tracking_gaps = copy.deepcopy(m.tracking_gaps)
    d.quality_score = m.quality_score
    d.quality_passed = m.quality_passed
    d.quality_warnings = list(m.quality_warnings)
    d.contact_marks = copy.deepcopy(m.contact_marks)
    d.trajectory = copy.deepcopy(m.trajectory)
    return d


def load_funscript_file(path: str | os.PathLike[str]) -> Document:
    """Go: LoadFunscriptFile (load a .funscript and convert to Document)."""
    s = load_funscript(path)
    d = from_funscript(s, "")
    # Chapters/bookmarks live outside the typed metadata - load separately.
    for attr, fn in (("chapters", load_chapters), ("bookmarks", load_bookmarks),
                     ("o_markers", load_o_markers)):
        try:
            setattr(d, attr, fn(path))
        except (SamnError, ValueError):
            pass
    try:
        d.normalize()
    except SamnError:
        pass
    return d


def _to_funscript(self: Document) -> Script:
    """Go: Document.ToFunscript (in-memory funscript incl. samn_axes)."""
    self.normalize()
    s = Script(actions=list(self.general))
    m = s.metadata
    m.creator = self.creator
    m.duration = self.duration_ms
    m.profile = self.profile
    m.quality_score = self.quality_score
    m.quality_passed = self.quality_passed
    m.quality_warnings = list(self.quality_warnings)
    m.tracking_gaps = copy.deepcopy(self.tracking_gaps)
    m.contact_marks = copy.deepcopy(self.contact_marks)
    m.trajectory = copy.deepcopy(self.trajectory)

    recipe = copy.deepcopy(self.recipe)
    recipe.playback_source = normalize_playback_source(self.playback_source)
    if recipe.sync == "" and is_distance_profile(self.profile):
        meta = recipe_meta(self.profile)
        if recipe.tick_ms == 0:
            recipe = meta
        else:
            recipe.sync = meta.sync
        recipe.playback_source = normalize_playback_source(self.playback_source)
    m.device_recipe = recipe

    if self.vibration or self.suction:
        m.samn_axes = SamnAxes(vibration=list(self.vibration), suction=list(self.suction))
    return s


Document.to_funscript = _to_funscript  # type: ignore[attr-defined]

# metadata keys export_funscript writes from the document (or drops)
EXPORT_MANAGED_META = (
    "creator", "duration", "profile", "device_recipe", "tracking_gaps",
    "contact_marks", "trajectory", "quality_score", "quality_passed",
    "quality_warnings", "chapters", "bookmarks", "oMarkers", "samn_axes",
)


def _sorted_map(m: dict[str, Any]) -> dict[str, Any]:
    """Go marshals maps with keys sorted (byte order)."""
    return {k: m[k] for k in sorted(m, key=lambda k: k.encode("utf-8", "surrogatepass"))}


def _read_json_object(path: str | os.PathLike[str]) -> dict[str, Any] | None:
    try:
        with open(path, "rb") as fh:
            b = fh.read()
    except OSError:
        return None
    return _decode_json_object(b)


def _decode_json_object(b: Any) -> dict[str, Any] | None:
    try:
        v = _loads(b, raw_numbers=True) if isinstance(b, (bytes, str)) else b
    except (ValueError, RecursionError):
        return None
    return v if isinstance(v, dict) else None


def _export_funscript(self: Document, path: str | os.PathLike[str]) -> None:
    """Go: Document.ExportFunscript.

    Writes a community .funscript (general -> actions, chapters/bookmarks in
    metadata, no Neo-2 axes).  Keys of an existing target that the export does
    not manage survive (audio_check, ai_opinion, OFS fields ...); a top-level
    "inverted" does not.  Atomic.
    """
    self.normalize()
    meta: dict[str, Any] = {"creator": self.creator, "duration": self.duration_ms}
    if self.profile != "":
        meta["profile"] = self.profile
    r = self.recipe
    if r.sync != "" or r.contact_vibration or r.tick_ms > 0:
        rec = copy.deepcopy(r)
        rec.playback_source = ""  # foreign tools always get recipe playback
        meta["device_recipe"] = _enc(rec)
    if self.tracking_gaps:
        meta["tracking_gaps"] = _enc(self.tracking_gaps)
    if self.contact_marks is not None:
        meta["contact_marks"] = _enc(self.contact_marks)
    if self.trajectory is not None and self.trajectory.tip:
        meta["trajectory"] = _enc(self.trajectory)
    if self.quality_score is not None:
        meta["quality_score"] = self.quality_score
    if self.quality_passed is not None:
        meta["quality_passed"] = self.quality_passed
    if self.quality_warnings:
        meta["quality_warnings"] = list(self.quality_warnings)
    if self.chapters:
        meta["chapters"] = _enc(self.chapters)
    if self.bookmarks:
        meta["bookmarks"] = _enc(self.bookmarks)
    if self.o_markers:
        meta["oMarkers"] = _enc(self.o_markers)

    doc: dict[str, Any] = {}
    old = _read_json_object(path)
    if old is not None:
        for k, v in old.items():
            if k not in ("actions", "metadata", "inverted"):
                doc[k] = v
        old_meta = _decode_json_object(old.get("metadata"))
        if old_meta is not None:
            for k in EXPORT_MANAGED_META:
                old_meta.pop(k, None)
            for k, v in old_meta.items():
                if k not in meta:
                    meta[k] = v
    doc["actions"] = _enc(self.general)
    doc["metadata"] = _sorted_map(meta)
    _write_atomic(path, _dump(_sorted_map(doc)).encode("utf-8"))


Document.export_funscript = _export_funscript  # type: ignore[attr-defined]


def _frames_to_actions(frames: list[Any], vibration: bool, max_step_ms: int) -> list[Point]:
    """Go: framesToActions (sparse 0-100 action list from mapper frames)."""
    if not frames:
        return [(0, 0)]

    def pos_of(f: Any) -> int:
        v = f.vibration if vibration else f.suction
        return max(0, min(100, _go_round(clamp01(v) * 100)))

    out: list[Point] = []
    last_at = -(1 << 62)
    last_pos = -1
    for f in frames:
        pos = pos_of(f)
        if last_pos < 0 or pos != last_pos or f.at - last_at >= max_step_ms:
            out.append((f.at, pos))
            last_at, last_pos = f.at, pos
    last = frames[-1]
    pos = pos_of(last)
    if out[-1][0] != last.at or out[-1][1] != pos:
        out.append((last.at, pos))
    return out


def bake_axes_from_recipe(actions: list[Point], opts: MapOptions,
                          max_step_ms: int = 200) -> tuple[list[Point], list[Point]]:
    """Go: funscript.BakeAxesFromRecipe -> (vibration, suction)."""
    opts = dataclasses.replace(opts, use_explicit_axes=False, vibration_axis=[], suction_axis=[])
    frames = to_intensity_curve(actions, opts) or []
    if max_step_ms <= 0:
        max_step_ms = 200
    return (_frames_to_actions(frames, True, max_step_ms),
            _frames_to_actions(frames, False, max_step_ms))


def _bake_neo_axes(self: Document) -> None:
    """Go: Document.BakeNeoAxes (vibration/suction from the recipe mapper)."""
    self.normalize()
    opts = recipe_for(self.profile)
    r = self.recipe
    if r.sync != "":
        try:
            opts.sync = parse_sync_mode(r.sync)
        except ValueError:
            pass
    if r.tick_ms > 0:
        opts.tick_ms = r.tick_ms
    if r.max_speed > 0:
        opts.max_speed = r.max_speed
    opts.smoothing = 0  # bake without recipe smoothing blur
    opts.min_suction = r.min_suction
    opts.contact_vibration = r.contact_vibration
    opts.contact_vibration_span = r.contact_vibration_span
    opts.contact_vibration_curve = r.contact_vibration_curve
    opts.scene_feel = _normalize_scene_feel(r.scene_feel)
    opts.scene_ranges = _normalize_scene_ranges(r.scene_ranges)
    opts.vibration_follows_suction = r.vibration_follows_suction
    opts.balance = r.feel_balance
    opts.tracking_gaps = self.tracking_gaps
    opts.contact_vibration_envelope = -1
    self.vibration, self.suction = bake_axes_from_recipe(self.general, opts, 100)
    self.playback_source = PLAYBACK_AXES
    self.recipe.playback_source = PLAYBACK_AXES


Document.bake_neo_axes = _bake_neo_axes  # type: ignore[attr-defined]


def _active_preset(self: Document | None) -> StrengthPreset | None:
    """Go: Document.ActivePreset (selected strength preset, or None)."""
    if self is None or self.active_strength == "":
        return None
    for p in self.strength_presets:
        if p.name == self.active_strength:
            return p
    return None


Document.active_preset = _active_preset  # type: ignore[attr-defined]


def _apply_contact_recipe(self: Document, enabled: bool, span: float, curve: str) -> None:
    """Go: Document.ApplyContactRecipe."""
    if self.recipe.sync == "":
        self.recipe.sync = default_sync_for_profile(self.profile)
    self.recipe.contact_vibration = enabled
    if enabled:
        self.recipe.contact_vibration_span = effective_contact_span(span)
        self.recipe.contact_vibration_curve = normalize_contact_curve(curve)
    else:
        self.recipe.contact_vibration_span = 0
        self.recipe.contact_vibration_curve = ""


def _apply_feel_recipe(self: Document, scene_feel: str, follow_suction: bool,
                       balance: float) -> None:
    """Go: Document.ApplyFeelRecipe."""
    if self.recipe.sync == "":
        self.recipe.sync = default_sync_for_profile(self.profile)
    self.recipe.scene_feel = _normalize_scene_feel(scene_feel)
    self.recipe.vibration_follows_suction = follow_suction
    self.recipe.feel_balance = _clamp_balance(balance)


Document.apply_contact_recipe = _apply_contact_recipe  # type: ignore[attr-defined]
Document.apply_feel_recipe = _apply_feel_recipe  # type: ignore[attr-defined]


def default_strength_presets() -> list[StrengthPreset]:
    """Go: DefaultStrengthPresets (soft / normal / strong)."""
    return [
        StrengthPreset("soft", 0.7, 0.85, 0.85, "soft"),
        StrengthPreset("normal", 1.0, 1.0),
        StrengthPreset("strong", 1.3, 1.1, 0.55, "peak"),
    ]
