# P5 — `.samn` v2: axis map and companion-file round trip

**Status:** spec, not built · input to EDIT-01 / D4 · 12 Oct 2026

## Why

`.samn` v1 (Free `docs/SAMN_FORMAT.md`) has three fixed curves — `general`,
`vibration`, `suction` — built for the Neo 2. Next needs any number of axes
(Handy stroke, SR6 L0–L2/R0–R2, several vibration motors) and must load and
write the multi-axis `.funscript` companion files other tools use. Nothing may
be lost when a v1 file, a companion pack, or an unknown future field passes
through Next.

## Rules

1. **Additive.** v2 = v1 + `axes`. Every v1 field keeps its meaning.
2. **v1 readers still work.** Next writes `general`, `vibration`, `suction` in
   the v1 places as well, so Free can open a Next file (it ignores `axes`).
3. **Unknown keys survive.** Fields Next does not know are kept and written back
   unchanged (top level and inside each axis).
4. **No invented axes.** An axis exists only if it was measured, imported or
   written by the user. Each axis records which.
5. **Script axes ≠ device channels.** Mapping a script axis to a device is the
   device adapter's job (P6/D2–D5), not stored here.

## Shape

```json
{
  "version": 2,
  "kind": "samnplayer.script",
  "general":   [{"at": 0, "pos": 20}],
  "vibration": [{"at": 0, "pos": 0}],
  "suction":   [],
  "axes": {
    "L0": { "semantic": "stroke", "source": "generated",
            "actions": [{"at": 0, "pos": 20}] },
    "R0": { "semantic": "twist", "source": "imported",
            "file": "clip.twist.funscript",
            "actions": [{"at": 0, "pos": 50}] },
    "V1": { "semantic": "vibrate", "source": "user",
            "actions": [{"at": 0, "pos": 0}] }
  }
}
```

### Axis ids

| Id | Semantic | Companion file suffix | Notes |
|---|---|---|---|
| `L0` | stroke | *(none)* `clip.funscript` | Mirrors `general` |
| `L1` | surge | `.surge.funscript` | |
| `L2` | sway | `.sway.funscript` | |
| `R0` | twist | `.twist.funscript` | |
| `R1` | roll | `.roll.funscript` | |
| `R2` | pitch | `.pitch.funscript` | |
| `V0` | vibrate | `.vib.funscript` | Mirrors `vibration` |
| `V1`… | vibrate, motor 2… | `.vib1.funscript` | New in v2 |
| `A0` | suction | `.suck.funscript` | Mirrors `suction` |
| `x-…` | any other | `.<name>.funscript` | Kept, shown as unknown lane |

Vibrate and suction suffixes are not standardised across tools. They are an
**export profile**, not a promise: the exporter lists what it wrote in a
manifest, and the profile can be changed (Blueprint §1.1).

`source` ∈ `generated | imported | user | derived`. A `derived` axis (e.g. vibe
computed from stroke by a recipe) is marked so the editor can show it as such.

### v1 ↔ v2 mirror

- On read: if `axes.L0` is missing, build it from `general`; same for
  `V0`/`vibration` and `A0`/`suction`.
- On write: `general` ← `L0`, `vibration` ← `V0`, `suction` ← `A0`.
- If both exist and differ on read, `axes` wins and a warning is logged.

## Companion round trip

**Import** `clip.funscript`: look beside it for `clip.<suffix>.funscript` from
the table; each becomes an axis with `source: imported` and `file` set.
Unrecognised `clip.<name>.funscript` files are imported as `x-<name>` with a
warning, never dropped.

**Export pack:** `clip.funscript` (L0) plus one companion per non-empty axis,
plus `clip.samn`. Collision policy: never overwrite without the user's setting;
write via temp file + rename (Free `funscript/atomic_write.go`).

## Tests (L1, ED-01/ED-02)

- Every Free v1 test fixture opens in v2 and saves back equal after JSON
  normalisation, except `version` and the added `axes`.
- v2 file opened by the Free v1 reader (`samn/io.go` at the import commit) keeps
  `general`/`vibration`/`suction` and all metadata.
- Companion pack L0/L1/L2/R0/R1/R2/V0/A0 → import → export → identical actions.
- Unknown top-level key, unknown axis key and unknown `x-` file survive a round trip.
- Conflicting `general` vs `axes.L0` → `axes` wins, warning emitted.
- Property test: random actions (sorted, duplicates, 0/100 edges) round trip with
  no time or position drift.
