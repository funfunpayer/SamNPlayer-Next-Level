# P3.8 comparison controller: first run (synthetic clips)

This is the first run of `samn_compare` (17 Oct), on the four synthetic clips of `P3_1_TRACK_PARITY.md`.
- **Reference:** the disc's true motion, written as a script.
- **"0.7":** 0.7's real `GenerateNativeCSRT` output for the same clip, box and profile (standard).

**What it shows:**
- **The controller works end to end.** It runs the manifest, the engine, the three pairs per clip, windows, flags and content hashes.
- **New and 0.7 are identical on these clips:** new vs 0.7 gives r 1.000 at lag 0, which matches `P3_2_PIPELINE_PARITY.md`.
- **The flags catch the GD-05 / GD-06 traps on purpose.**
  - On the short 1 Hz clips, lag 0 and −1000 ms fit equally well, because the period equals the lag window. The search keeps the first of the tied lags (no tie-breaking, D-065). So the whole-clip lag reads −1000 ms. It is marked **aliasing** and **pinned at the edge**, so it is not trusted.
  - On the 5-minute clip (0.7 Hz), lag 0 is found, and 5 of 30 windows carry the aliasing flag.
- **A fair score needs non-periodic content and real clips.** The Owner's golden clips run locally with a manifest; the media never enter the repository.

Repeat: `samn_compare <manifest.json> <out-folder>` writes `report.json` and `report.md`.

## Report

Motion Fidelity against the reference (not the Quality Doctor; the two do not predict each other, GD-04). Lag window +-1000 ms, windows of 10 s, OpenCV 4.6.0.

| clip | profile | pair | r | lag ms | orient. | n | windowed r (median) | windows | flags |
|---|---|---|---|---|---|---|---|---|---|
| moving | standard | new vs reference | 0.992 | -1000 | normal | 70 | 0.992 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| moving | standard | 0.7 vs reference | 0.992 | -1000 | normal | 70 | 0.992 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| moving | standard | new vs 0.7 | 1.000 | 0 | normal | 80 | 1.000 | 1/1 | aliasing, 1 windows aliasing |
| panning | standard | new vs reference | 0.993 | -1000 | normal | 90 | 0.993 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| panning | standard | 0.7 vs reference | 0.993 | -1000 | normal | 90 | 0.993 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| panning | standard | new vs 0.7 | 1.000 | 0 | normal | 100 | 1.000 | 1/1 | aliasing, 1 windows aliasing |
| cut | standard | new vs reference | 0.992 | -1000 | normal | 70 | 0.992 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| cut | standard | 0.7 vs reference | 0.992 | -1000 | normal | 70 | 0.992 | 1/1 | aliasing, lag pinned at the edge, 1 windows aliasing, 1 windows pinned |
| cut | standard | new vs 0.7 | 1.000 | 0 | normal | 80 | 1.000 | 1/1 | aliasing, 1 windows aliasing |
| long | standard | new vs reference | 0.991 | 0 | normal | 3000 | 0.991 | 30/30 | 5 windows aliasing |
| long | standard | 0.7 vs reference | 0.991 | 0 | normal | 3000 | 0.991 | 30/30 | 5 windows aliasing |
| long | standard | new vs 0.7 | 1.000 | 0 | normal | 3000 | 1.000 | 30/30 | – |

Manifest (content hashes):

- moving: video `8601f966072dd17d`, reference `ed3dba247293fc4b`, 0.7 `00b296383da46341`, box 100,40,120,160, frames 200, lost 0
- panning: video `087857cee2e09094`, reference `d21da9d84fdc2eb4`, 0.7 `bbf7296b75a11f11`, box 100,40,120,160, frames 250, lost 0
- cut: video `397e0bdb582101a5`, reference `ed3dba247293fc4b`, 0.7 `fd87c11c5c30a0ed`, box 100,40,120,160, frames 200, lost 0
- long: video `b18b89e15b8c36b9`, reference `e0ab84bd25d24838`, 0.7 `3f5b448e9127756c`, box 100,40,120,160, frames 7500, lost 0
