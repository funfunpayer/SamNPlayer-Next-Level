# samn.formats

File formats: `.funscript` today; `.samn`, OFS projects and settings follow
in P1.

- **API:** `funscript.load(path) -> Funscript(path, actions, inverted, dropped)`.
- **Depends on:** `samn.native` (C++ `funscript_native` parser).
- **Go reference:** `funscript/` and `samn/` in `funfunpayer/SamNPlayer`.
- **Status:** P0 (load only).
