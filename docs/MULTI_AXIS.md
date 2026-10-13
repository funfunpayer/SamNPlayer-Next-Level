# Multi-axis / Neo 2 curves

Superseded by the native script format — see [`docs/SAMN_FORMAT.md`](SAMN_FORMAT.md).

Summary: `.samn` holds general + vibration + suction + recipe + strength
presets; `.funscript` is import/export (general stroke only). In-memory
playback still uses `funscript.Script` + optional `samn_axes` via
`Document.ToFunscript()`.
