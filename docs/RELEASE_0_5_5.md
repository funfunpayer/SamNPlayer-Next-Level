# Release 0.5.5 — stack ship (GUI + OFS + OpenCV5 + autotune)

Ships the stacked work from #97–#99 on top of 0.5.4: brand/GUI refresh,
Go Tf/Tj + auto-pipeline, OFS-inspired editor tools, OpenCV 5 tracker
fallback (#95), and the Flow→CSRT→Autotune funscript workflow.

## Ship contents

- OpenCV 5 CSRT→KCF fallback (#94/#95) — generate works on Windows 5.0.0  
- Autotune profile + workflow tip (`docs/FUNSCRIPT_ALGOS.md`)  
- OFS tools in Go: speed highlights, bookmarks/chapters, heatmap PNG, `.snp.json`  
- CapSpeedRange / playlist / connect-fail / float OFS times bugfixes  
- Editor reload after speed-cap; ROI redraw preserves manual backend  
- Startup dirs/deps, optional connect smoke test, EO amplitude-only  

## Your smoke tests

### Generate

1. CSRT or Tf/Tj on a short clip → funscript + Quality Doctor score.  
2. Backend **Flow** then redraw ROI → Flow stays selected.  
3. Profil **Autotune** → log mentions detrend/bandpass/max_speed.  
4. OpenCV 5 machines: generate must not raise `CSRT-Tracker nicht gefunden`.

### Playback / OFS

1. Load script → Editieren an → Speed-Cap on a heatmap range → edit again  
   and save: capped curve must remain (not revert to pre-cap points).  
2. Film-Liste: video end advances when Auto-Weiter on; connect-fail does not.  
3. Heatmap PNG / Projekt `.snp.json` / Bookmark float times from OFS imports.

### Clip (ffmpeg)

```text
go test ./generator -run 'TestGenerateNativeSimpleEndToEnd|TestGenerateNativeTwoPointSimple' -count=1
python3 generator/create_tracker_test.py
```

## Explicitly later (not blocking 0.5.5)

- Real FunGen golden-clip parity (needs your clips)  
- Wire `find_two_rois` into generation (needs measurement)  
- Neo 2 feel validation on hardware  
- NVOFA / YOLO script-writer paths  

See `CHANGELOG.md` [0.5.5], `docs/FUNSCRIPT_ALGOS.md`, `docs/OFS_LEARN.md`.
