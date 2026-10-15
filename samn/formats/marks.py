"""Funscript bookmarks / chapters metadata (OFS-compatible).

Port of SamNPlayer ``funscript/bookmarks.go``.  Reads and writes
``metadata.bookmarks`` and ``metadata.chapters`` of a ``.funscript`` file.
Saving replaces only ``metadata.<key>``; every other key (top level and
inside ``metadata``) is kept.  Like the Go code, a save re-serialises the
file as compact JSON with sorted keys at the top level and inside
``metadata`` (``json.Marshal`` of a ``map``), while values that are not
touched keep their original text and key order (Go ``json.RawMessage``).
"""

from __future__ import annotations

import json
import math
import os
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

__all__ = [
    "Bookmark",
    "Chapter",
    "MarksError",
    "flex_time_ms",
    "load_bookmarks",
    "load_chapters",
    "normalize_time_to_ms",
    "parse_clock_ms",
    "patch_metadata_key",
    "read_metadata_map",
    "save_bookmarks",
    "save_chapters",
]


class MarksError(ValueError):
    """Unreadable file / invalid JSON / invalid metadata (Go: returned error)."""


@dataclass
class Bookmark:
    """Go ``Bookmark``: named time marker (``time_ms`` is JSON ``time``)."""

    name: str
    time_ms: int


@dataclass
class Chapter:
    """Go ``ChapterMark``: named span; ``end_ms`` 0 means open-ended
    (JSON ``startTime`` / ``endTime``, ``endTime`` omitted when 0)."""

    name: str
    start_ms: int
    end_ms: int = 0


# --------------------------------------------------------------------------
# Time parsing
# --------------------------------------------------------------------------

# Go strconv.ParseFloat (decimal forms; no surrounding spaces, no underscores).
_FLOAT_RE = re.compile(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?\Z")
_SPECIAL_RE = re.compile(r"([+-]?)(inf|infinity)\Z|nan\Z", re.IGNORECASE)


def _round_half_away(x: float) -> int:
    """Go ``math.Round`` (half away from zero); Python's round() is banker's."""
    t = math.trunc(x)
    if abs(x - t) >= 0.5:
        t += 1 if x > 0 else -1
    return int(t)


def _parse_float(s: str) -> float | None:
    """Go ``strconv.ParseFloat(s, 64)``; None where Go returns an error
    (incl. out-of-range values).  Hex floats are not supported."""
    if _FLOAT_RE.match(s):
        v = float(s)
        return None if math.isinf(v) else v
    if _SPECIAL_RE.match(s):
        return float(s)
    return None


def parse_clock_ms(s: str) -> int | None:
    """Go ``parseClockMs``: "[[H:]M:]S[.fff]" ("00:01:23.456", "1:23.5") as ms.

    Returns None where Go returns ``ok == false``.
    """
    parts = s.strip().split(":")
    if len(parts) < 2 or len(parts) > 3:
        return None
    total = 0.0
    for i, p in enumerate(parts):
        v = _parse_float(p)
        if v is None or v < 0 or math.isnan(v) or math.isinf(v):
            return None
        if i < len(parts) - 1 and v != math.trunc(v):
            return None  # only the seconds part may have a fraction
        total = total * 60 + v
    return _round_half_away(total * 1000)


def normalize_time_to_ms(v: float) -> int:
    """Go ``normalizeTimeToMs``: values under 1e5 with a fractional part look
    like seconds (OFS often uses 12.5); everything else stays ms."""
    if math.isnan(v) or math.isinf(v):
        return 0
    a = abs(v)
    frac = a - math.trunc(a)
    if a < 100000 and frac > 1e-9:
        return _round_half_away(v * 1000)
    return _round_half_away(v)


def flex_time_ms(value: Any) -> int:
    """Go ``flexTimeMs.UnmarshalJSON`` on an already-parsed JSON value.

    null -> 0; number -> normalize_time_to_ms; string -> number text or clock
    text ("HH:MM:SS.mmm").  Anything else (and unreadable text) raises
    MarksError, exactly where Go returns an error.
    """
    if value is None:
        return 0
    if isinstance(value, str):
        v = _parse_float(value)
        if v is None:
            ms = parse_clock_ms(value)
            if ms is None:
                raise MarksError(f"ungültige Zeitangabe: {value!r}")
            return ms
        return normalize_time_to_ms(v)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MarksError(f"ungültige Zeitangabe: {value!r}")
    try:
        f = float(value)
    except OverflowError as exc:
        raise MarksError(f"Zeitangabe außerhalb des Bereichs: {value!r}") from exc
    if math.isinf(f):
        raise MarksError(f"Zeitangabe außerhalb des Bereichs: {value!r}")
    return normalize_time_to_ms(f)


# --------------------------------------------------------------------------
# Raw JSON helpers (Go: map[string]json.RawMessage + json.Marshal)
# --------------------------------------------------------------------------

def _reject_constant(name: str) -> Any:
    raise ValueError(f"invalid JSON constant {name}")


def _loads(text: str) -> Any:
    return json.loads(text, parse_constant=_reject_constant)


_WS = " \t\n\r"
_COMPACT_RE = re.compile(r'"(?:[^"\\]|\\.)*"|[ \t\n\r]+', re.DOTALL)
_HTML_ESC = {"<": "\\u003c", ">": "\\u003e", "&": "\\u0026",
             " ": "\\u2028", " ": "\\u2029"}
_HTML_RE = re.compile("[<>&  ]")


def _html_escape(s: str) -> str:
    return _HTML_RE.sub(lambda m: _HTML_ESC[m.group(0)], s)


def _go_string(s: str) -> str:
    """Go json.Marshal of a string (non-ASCII kept, <>& escaped)."""
    s = re.sub("[\ud800-\udfff]", "�", s)
    return _html_escape(json.dumps(s, ensure_ascii=False))


def _compact(raw: str) -> str:
    """Go compacts a RawMessage when marshalling: drop whitespace outside
    strings and HTML-escape inside them; numbers/key order stay untouched."""

    def sub(m: re.Match[str]) -> str:
        t = m.group(0)
        return _html_escape(t) if t[0] == '"' else ""

    return _COMPACT_RE.sub(sub, raw)


def _split_object(text: str) -> dict[str, str]:
    """Split a *valid* JSON object into {key: raw value text} (last duplicate
    wins, like Go).  Equivalent of unmarshalling into map[string]RawMessage."""
    dec = json.JSONDecoder()
    out: dict[str, str] = {}
    i = text.index("{") + 1
    n = len(text)
    while True:
        while i < n and text[i] in _WS:
            i += 1
        if text[i] == "}":
            return out
        if text[i] == ",":
            i += 1
            continue
        key, i = dec.raw_decode(text, i)
        while text[i] in _WS or text[i] == ":":
            i += 1
        _, end = dec.raw_decode(text, i)
        out[key] = text[i:end]
        i = end


def _marshal_object(obj: dict[str, str]) -> str:
    """Go json.Marshal of map[string]RawMessage (sorted keys, compact)."""
    return "{" + ",".join(
        f"{_go_string(k)}:{_compact(obj[k])}" for k in sorted(obj)) + "}"


def _read_doc(path: str | os.PathLike[str]) -> tuple[dict[str, str] | None, str]:
    try:
        data = Path(path).read_bytes()
    except OSError as exc:
        raise MarksError(f"funscript: Datei konnte nicht gelesen werden: {exc}") from exc
    text = data.decode("utf-8", errors="replace")
    try:
        parsed = _loads(text)
    except ValueError as exc:
        raise MarksError(f"funscript: ungültiges JSON: {exc}") from exc
    if parsed is None:  # Go: nil map
        return None, text
    if not isinstance(parsed, dict):
        raise MarksError("funscript: ungültiges JSON: kein Objekt")
    return _split_object(text), text


def _metadata_of(doc: dict[str, str] | None) -> dict[str, str]:
    raw = (doc or {}).get("metadata")
    if raw is None or raw == "null":
        return {}
    try:
        if not isinstance(_loads(raw), dict):
            raise ValueError("kein Objekt")
    except ValueError as exc:
        raise MarksError(f"funscript: metadata ungültig: {exc}") from exc
    return _split_object(raw)


def read_metadata_map(path: str | os.PathLike[str]) -> dict[str, str]:
    """Go ``readMetadataMap``: ``metadata`` as {key: raw JSON text}."""
    doc, _ = _read_doc(path)
    return _metadata_of(doc)


def _write_atomic(path: str | os.PathLike[str], data: bytes, mode: int = 0o644) -> None:
    """Go ``writeFileAtomic``: temp file in the same dir, fsync, os.replace."""
    p = Path(path)
    fd, tmp = tempfile.mkstemp(dir=p.parent, prefix=f".{p.name}.tmp-")
    try:
        with os.fdopen(fd, "wb") as f:
            if hasattr(os, "fchmod"):  # POSIX only; Windows has no file modes to set
                os.fchmod(f.fileno(), mode)
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, p)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def patch_metadata_key(path: str | os.PathLike[str], key: str, encoded: str) -> None:
    """Go ``patchMetadataKey``: set ``metadata[key]`` (``encoded`` is JSON
    text), keep all other keys, write atomically."""
    doc, _ = _read_doc(path)
    if doc is None:
        raise MarksError("funscript: ungültiges JSON: kein Objekt")
    meta = _metadata_of(doc)
    meta[key] = encoded
    doc["metadata"] = _marshal_object(meta)
    out = _marshal_object(doc).encode("utf-8")
    try:
        _write_atomic(path, out, 0o644)
    except OSError as exc:
        raise MarksError(f"funscript: Datei konnte nicht geschrieben werden: {exc}") from exc


# --------------------------------------------------------------------------
# Wire decoding (Go bookmarkWire / chapterWire, case-insensitive field names)
# --------------------------------------------------------------------------

def _load_array(raw: str, what: str) -> list[Any]:
    try:
        value = _loads(raw)
        if not isinstance(value, list):
            raise ValueError("kein Array")
        return value
    except ValueError as exc:
        raise MarksError(f"funscript: {what} ungültig: {exc}") from exc


def _fields(item: Any, names: dict[str, str]) -> dict[str, Any]:
    """Go struct decoding: null -> zero value, object -> case-insensitive
    field match (later keys override), anything else is an error."""
    if item is None:
        return {}
    if not isinstance(item, dict):
        raise ValueError("kein Objekt")
    out: dict[str, Any] = {}
    for k, v in item.items():
        field = names.get(k.lower())
        if field is not None:
            out[field] = v
    return out


def _name(f: dict[str, Any]) -> str:
    v = f.get("name")
    if v is None:
        return ""
    if not isinstance(v, str):
        raise ValueError("name ist kein Text")
    return v


def load_bookmarks(path: str | os.PathLike[str]) -> list[Bookmark]:
    """Go ``LoadBookmarks``: read metadata.bookmarks without rewriting the file."""
    raw = read_metadata_map(path).get("bookmarks")
    if raw is None or raw == "null":
        return []
    out: list[Bookmark] = []
    try:
        for item in _load_array(raw, "bookmarks"):
            f = _fields(item, {"name": "name", "time": "time"})
            out.append(Bookmark(_name(f), flex_time_ms(f.get("time"))))
    except ValueError as exc:  # MarksError is a ValueError
        if isinstance(exc, MarksError) and str(exc).startswith("funscript:"):
            raise
        raise MarksError(f"funscript: bookmarks ungültig: {exc}") from exc
    return out


def load_chapters(path: str | os.PathLike[str]) -> list[Chapter]:
    """Go ``LoadChapters``: read metadata.chapters."""
    raw = read_metadata_map(path).get("chapters")
    if raw is None or raw == "null":
        return []
    out: list[Chapter] = []
    names = {"name": "name", "starttime": "start", "endtime": "end"}
    try:
        for item in _load_array(raw, "chapters"):
            f = _fields(item, names)
            out.append(Chapter(_name(f), flex_time_ms(f.get("start")),
                               flex_time_ms(f.get("end"))))
    except ValueError as exc:
        if isinstance(exc, MarksError) and str(exc).startswith("funscript:"):
            raise
        raise MarksError(f"funscript: chapters ungültig: {exc}") from exc
    return out


# --------------------------------------------------------------------------
# Saving
# --------------------------------------------------------------------------

def save_bookmarks(path: str | os.PathLike[str], bookmarks: list[Bookmark]) -> None:
    """Go ``SaveBookmarks``: replace metadata.bookmarks (sorted by time),
    keeping other metadata keys.  An empty list is written as ``null``
    (Go marshals a nil slice that way)."""
    ordered = sorted(bookmarks, key=lambda b: b.time_ms)
    if not ordered:
        encoded = "null"
    else:
        encoded = "[" + ",".join(
            f'{{"name":{_go_string(b.name)},"time":{int(b.time_ms)}}}'
            for b in ordered) + "]"
    patch_metadata_key(path, "bookmarks", encoded)


def save_chapters(path: str | os.PathLike[str], chapters: list[Chapter]) -> None:
    """Go ``SaveChapters``: replace metadata.chapters (sorted by start);
    ``endTime`` is omitted when 0, an empty list is written as ``null``."""
    ordered = sorted(chapters, key=lambda c: c.start_ms)
    if not ordered:
        encoded = "null"
    else:
        items = []
        for c in ordered:
            s = f'{{"name":{_go_string(c.name)},"startTime":{int(c.start_ms)}'
            if c.end_ms:
                s += f',"endTime":{int(c.end_ms)}'
            items.append(s + "}")
        encoded = "[" + ",".join(items) + "]"
    patch_metadata_key(path, "chapters", encoded)
