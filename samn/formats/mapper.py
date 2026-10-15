"""Funscript -> device-intensity mapper (port of SamNPlayer funscript/mapper.go).

Actions are ``(at_ms, pos)`` int tuples sorted by time.  Go names are given in
each docstring.  Not ported: recipe/metadata plumbing (MapOptionsFromScript)
and the geometry behind SpatialContactIntensity; the latter is injectable via
``MapOptions.spatial_contact`` (a callable ``at_ms -> 0..1``).
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Callable, Sequence

Action = tuple[int, int]


class SyncMode(IntEnum):
    """Go: SyncMode."""
    INDEPENDENT = 0
    SYNCHRONIZED = 1
    ALTERNATING = 2
    VIBRATION_ONLY = 3
    SUCTION_ONLY = 4
    SUCTION_POSITION = 5

    def __str__(self) -> str:  # Go: SyncMode.String
        return _SYNC_NAMES[self]


_SYNC_NAMES = {
    SyncMode.INDEPENDENT: "independent",
    SyncMode.SYNCHRONIZED: "synchronized",
    SyncMode.ALTERNATING: "alternating",
    SyncMode.VIBRATION_ONLY: "vibration_only",
    SyncMode.SUCTION_ONLY: "suction_only",
    SyncMode.SUCTION_POSITION: "suction_position",
}


def parse_sync_mode(s: str) -> SyncMode:
    """Go: ParseSyncMode. Raises ValueError for unknown names."""
    if s == "":
        return SyncMode.INDEPENDENT
    for mode, name in _SYNC_NAMES.items():
        if name == s:
            return mode
    raise ValueError(f"funscript: unbekannter sync-mode {s!r}")


@dataclass
class Frame:
    """Go: Frame."""
    at: int
    vibration: float
    suction: float


@dataclass
class TrackingGap:
    """Go: TrackingGap (inclusive window, ms)."""
    start_ms: int
    end_ms: int


@dataclass
class SceneFeelRange:
    """Go: SceneFeelRange (start inclusive, end exclusive)."""
    start_ms: int
    end_ms: int
    feel: str


CONTACT_CURVE_LINEAR = "linear"
CONTACT_CURVE_SOFT = "soft"
CONTACT_CURVE_PEAK = "peak"
CONTACT_CURVE_IMPULSE = "impulse"

IMPULSE_CONTACT_THRESHOLD = 0.55
SPATIAL_DEPTH_WEIGHT = 0.30
SPATIAL_CONTACT_ENVELOPE_SMOOTH = 0.18
DEFAULT_CONTACT_ENVELOPE_SMOOTH = 0.45
DEFAULT_CONTACT_VIBRATION_SPAN = 0.75
CONTACT_VIBRATION_MIN_SPAN = 5.0

SCENE_FEEL_NONE = ""
SCENE_FEEL_ORAL = "oral"
SCENE_FEEL_INTERCOURSE = "intercourse"
SCENE_FEEL_BREAST = "breast"
SCENE_FEEL_HAND = "hand"

# (floor, range, depth_w, speed_w)
_SCENE_SHAPES = {
    SCENE_FEEL_ORAL: (0.30, 0.70, 0.65, 0.35),
    SCENE_FEEL_INTERCOURSE: (0.15, 0.85, 0.45, 0.55),
    SCENE_FEEL_BREAST: (0.20, 0.55, 0.70, 0.30),
    SCENE_FEEL_HAND: (0.10, 0.70, 0.25, 0.75),
}


@dataclass
class MapOptions:
    """Go: MapOptions (axes as lists of (at, pos) tuples)."""
    tick_ms: int = 50
    max_speed: float = 0.6
    min_vibration: float = 0.15
    min_suction: float = 0.0
    smoothing: float = 0.3
    sync: SyncMode = SyncMode.INDEPENDENT
    contact_vibration: bool = False
    contact_vibration_span: float = 0.0
    contact_vibration_curve: str = ""
    contact_vibration_envelope: float = 0.0
    tracking_gaps: list[TrackingGap] = field(default_factory=list)
    scene_feel: str = ""
    scene_ranges: list[SceneFeelRange] = field(default_factory=list)
    vibration_follows_suction: bool = False
    balance: float = 0.0
    use_explicit_axes: bool = False
    vibration_axis: list[Action] = field(default_factory=list)
    suction_axis: list[Action] = field(default_factory=list)
    # Replacement for Go's marks/trajectory spatial model: at_ms -> 0..1.
    spatial_contact: Callable[[int], float] | None = None


def default_map_options() -> MapOptions:
    """Go: DefaultMapOptions."""
    return MapOptions(tick_ms=50, max_speed=0.6, min_vibration=0.15,
                      min_suction=0.0, smoothing=0.3, sync=SyncMode.INDEPENDENT)


def normalize_contact_curve(name: str) -> str:
    """Go: NormalizeContactCurve."""
    n = name.strip().lower()
    if n in (CONTACT_CURVE_SOFT, "soft_entry", "weicher"):
        return CONTACT_CURVE_SOFT
    if n in (CONTACT_CURVE_PEAK, "strong_peak", "peaky"):
        return CONTACT_CURVE_PEAK
    if n in (CONTACT_CURVE_IMPULSE, "events", "contact_events", "peak_emphasis"):
        return CONTACT_CURVE_IMPULSE
    return CONTACT_CURVE_LINEAR


def effective_contact_span(span: float) -> float:
    """Go: EffectiveContactSpan."""
    if span <= 0 or span >= 1:
        return DEFAULT_CONTACT_VIBRATION_SPAN
    if span < 0.4:
        return 0.4
    if span > 0.95:
        return 0.95
    return span


def clamp01(v: float) -> float:
    """Go: clamp01."""
    if v < 0:
        return 0.0
    if v > 1:
        return 1.0
    return v


def lift_floor(value: float, floor: float) -> float:
    """Go: liftFloor (scales value into [floor, 1], does not clip)."""
    if floor <= 0:
        return value
    if floor >= 1:
        return 1.0
    return floor + value * (1 - floor)


def apply_contact_curve(t: float, curve: str) -> float:
    """Go: applyContactCurve."""
    t = clamp01(t)
    c = normalize_contact_curve(curve)
    if c == CONTACT_CURVE_SOFT:
        return t * t
    if c == CONTACT_CURVE_PEAK:
        return math.sqrt(t)
    if c == CONTACT_CURVE_IMPULSE:
        if t < IMPULSE_CONTACT_THRESHOLD:
            return 0.0
        u = (t - IMPULSE_CONTACT_THRESHOLD) / (1 - IMPULSE_CONTACT_THRESHOLD)
        return u * u * u
    return t


def clamp_balance(b: float) -> float:
    """Go: ClampBalance (NaN -> 0)."""
    if b != b:
        return 0.0
    return max(-1.0, min(1.0, b))


def balance_scales(balance: float) -> tuple[float, float]:
    """Go: BalanceScales -> (vib, suc)."""
    balance = clamp_balance(balance)
    vib = suc = 1.0
    if balance > 0:
        vib = 1 - balance
    elif balance < 0:
        suc = 1 + balance
    return vib, suc


def scene_suction(feel: str, depth: float, intensity: float) -> tuple[float, bool]:
    """Go: SceneSuction -> (value, ok)."""
    s = _SCENE_SHAPES.get(feel)
    if s is None:
        return 0.0, False
    floor, rng, dw, sw = s
    return clamp01(floor + rng * (dw * clamp01(depth) + sw * clamp01(intensity))), True


def _scene_feel_at(opts: MapOptions, t: int) -> str:
    """Go: sceneFeelAt."""
    for r in opts.scene_ranges:
        if r.start_ms <= t < r.end_ms:
            return r.feel
    return opts.scene_feel


def sample_axis_pos(actions: Sequence[Action], t: int) -> float:
    """Go: SampleAxisPos (linear interpolation, empty -> 0)."""
    if not actions:
        return 0.0
    if t <= actions[0][0]:
        return float(actions[0][1])
    last = actions[-1]
    if t >= last[0]:
        return float(last[1])
    lo, hi = 0, len(actions) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2  # non-negative, so floor == Go truncation
        if actions[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    a, b = actions[lo], actions[hi]
    dt = b[0] - a[0]
    if dt <= 0:
        return float(a[1])
    frac = float(t - a[0]) / float(dt)
    return float(a[1]) + frac * float(b[1] - a[1])


def playback_actions(actions: Sequence[Action], inverted: bool = False) -> list[Action]:
    """Go: Script.PlaybackActions (pos -> 100-pos, clamped, when inverted)."""
    if not inverted or not actions:
        return list(actions)
    return [(at, max(0, min(100, 100 - pos))) for at, pos in actions]


def to_intensity_curve(actions: Sequence[Action], opts: MapOptions,
                       inverted: bool = False) -> list[Frame] | None:
    """Go: Script.ToIntensityCurve. Returns None (Go nil) for < 2 actions."""
    acts = playback_actions(actions, inverted)
    if len(acts) < 2:
        return None
    tick_ms = opts.tick_ms if opts.tick_ms > 0 else 50
    max_speed = opts.max_speed if opts.max_speed > 0 else 0.6
    duration = actions[-1][0]  # Script.Duration uses the stored actions
    frames: list[Frame] = []
    seg_idx = 0
    prev_vib = prev_suc = 0.0
    prev_contact_vib = 0.0
    envelope = opts.contact_vibration_envelope
    envelope_on = True
    if envelope < 0:
        envelope_on = False
    elif envelope == 0 or envelope >= 1:
        envelope = DEFAULT_CONTACT_ENVELOPE_SMOOTH

    contact_min = contact_max = 0.0
    contact_enabled = opts.contact_vibration
    contact_span = effective_contact_span(opts.contact_vibration_span)
    contact_curve = normalize_contact_curve(opts.contact_vibration_curve)
    gaps = opts.tracking_gaps
    if contact_enabled:
        positions = [float(p) for _, p in acts]
        pos_min, pos_max = min(positions), max(positions)
        if pos_max - pos_min < CONTACT_VIBRATION_MIN_SPAN:
            contact_enabled = False
        else:
            contact_min = pos_min + contact_span * (pos_max - pos_min)
            contact_max = pos_max

    def in_gap(t: int) -> bool:
        return any(g.start_ms <= t <= g.end_ms for g in gaps)

    spatial_on = opts.contact_vibration and opts.spatial_contact is not None
    last_spatial_hit = False

    def contact_vib_at(pos: float, t: int) -> float:
        nonlocal last_spatial_hit
        last_spatial_hit = False
        if (not contact_enabled and not spatial_on) or in_gap(t):
            return 0.0
        depth = 0.0
        if contact_enabled and pos >= contact_min:
            linear = clamp01((pos - contact_min) / (contact_max - contact_min))
            depth = apply_contact_curve(linear, contact_curve)
        spatial = 0.0
        if spatial_on:
            spatial = apply_contact_curve(opts.spatial_contact(t), contact_curve)
        vib = depth
        if spatial_on:
            weighted = depth * SPATIAL_DEPTH_WEIGHT
            vib = weighted
            if spatial > vib:
                vib = spatial
            last_spatial_hit = spatial > 0 and spatial >= weighted
        if vib > 0 and opts.min_vibration > 0:
            vib = lift_floor(vib, opts.min_vibration)
        return vib

    def smooth_contact(vib: float, t: int) -> float:
        nonlocal prev_contact_vib
        if (not contact_enabled and not spatial_on) or not envelope_on or in_gap(t):
            if in_gap(t):
                prev_contact_vib = 0.0
            return vib
        env = envelope
        if last_spatial_hit and SPATIAL_CONTACT_ENVELOPE_SMOOTH < env:
            env = SPATIAL_CONTACT_ENVELOPE_SMOOTH
        if frames:
            vib = env * prev_contact_vib + (1 - env) * vib
        prev_contact_vib = vib
        return vib

    sync = opts.sync
    t = 0
    while t <= duration:
        if opts.use_explicit_axes:
            vib = clamp01(sample_axis_pos(opts.vibration_axis, t) / 100.0)
            suc = clamp01(sample_axis_pos(opts.suction_axis, t) / 100.0)
            if in_gap(t):
                vib = 0.0
        else:
            while seg_idx < len(acts) - 2 and acts[seg_idx + 1][0] <= t:
                seg_idx += 1
            a, b = acts[seg_idx], acts[seg_idx + 1]
            dt = b[0] - a[0]
            if dt <= 0:
                dt = 1
            speed = abs(float(b[1] - a[1])) / float(dt)
            intensity = clamp01(speed / max_speed)
            frac = float(t - a[0]) / float(dt)
            pos = float(a[1]) + frac * float(b[1] - a[1])
            pos_signal = clamp01(pos / 100.0)
            if sync == SyncMode.SYNCHRONIZED:
                vib, suc = intensity, intensity
            elif sync == SyncMode.ALTERNATING:
                vib, suc = intensity, clamp01(1 - intensity)
            elif sync == SyncMode.VIBRATION_ONLY:
                vib, suc = intensity, 0.0
            elif sync == SyncMode.SUCTION_ONLY:
                vib, suc = 0.0, intensity
            elif sync == SyncMode.SUCTION_POSITION:
                vib, suc = 0.0, pos_signal
                if contact_enabled or spatial_on:
                    vib = contact_vib_at(pos, t)
                    vib = smooth_contact(vib, t)
                elif in_gap(t):
                    vib = 0.0
            else:
                vib, suc = intensity, pos_signal
                ss, ok = scene_suction(_scene_feel_at(opts, t), pos_signal, intensity)
                if ok:
                    suc = ss
                if contact_enabled or spatial_on:
                    cv = contact_vib_at(pos, t)
                    if cv > 0:
                        vib = smooth_contact(cv, t)
                    elif in_gap(t):
                        vib = 0.0
                        prev_contact_vib = 0.0
                    else:
                        prev_contact_vib = 0.0
            if sync not in (SyncMode.SUCTION_ONLY, SyncMode.SUCTION_POSITION):
                vib = lift_floor(vib, opts.min_vibration)
            if sync not in (SyncMode.VIBRATION_ONLY, SyncMode.SUCTION_POSITION):
                suc = lift_floor(suc, opts.min_suction)
            if (opts.vibration_follows_suction
                    and sync not in (SyncMode.SUCTION_ONLY, SyncMode.VIBRATION_ONLY)):
                vib = suc
            if opts.balance != 0:
                vs, ss2 = balance_scales(opts.balance)
                vib *= vs
                suc *= ss2
        if opts.smoothing > 0 and frames:
            vib = opts.smoothing * prev_vib + (1 - opts.smoothing) * vib
            suc = opts.smoothing * prev_suc + (1 - opts.smoothing) * suc
        if in_gap(t) and (sync == SyncMode.SUCTION_POSITION
                          or opts.use_explicit_axes or opts.contact_vibration):
            vib = 0.0
        prev_vib, prev_suc = vib, suc
        frames.append(Frame(at=t, vibration=vib, suction=suc))
        t += tick_ms
    return frames
