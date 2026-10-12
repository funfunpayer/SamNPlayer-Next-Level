# Release 0.5.3 — test release checklist

This release is a **test bed** for later work (Go path, logs, CLI generate),
not a claim that FunGen parity or multi-device is done.

## Ship contents

- Automatic Go generate for default CSRT (+ Auto-Retry in Go) — #89  
- Soft Python fallback on Go failure — #90  
- simpletrack Windows path without Python — v0.5.2  
- **Log tab** (copyable protocol) + pipeline label (Go vs Python)  
- CLI: `SamNPlayer generate --video … --roi x,y,w,h`  

## Your smoke tests (hardware / real clips)

1. **Windows GUI without Python:** one ROI, CSRT defaults → generate.  
   Expect status/Log: `Pfad: Go (simpletrack/…)`. Copy a line from Log tab.  
2. **Linux GUI with OpenCV build:** same → `trackcv` / CSRT.  
3. **Force special case:** enable Audio-Check or non-CSRT backend → Python path or clear error.  
4. **CLI:** `SamNPlayer generate --video clip.mp4 --roi 100,80,60,60 --max-frames 50`  
5. **Device (Neo 2):** connect, diagnostics, short playback.  
6. **If Python missing on Windows:** default generate still works; AI-Train / exotic backends explain missing Python.

## Explicitly later (not blocking 0.5.3)

- FunGen real-clip goldens  
- SAM format as default  
- Other devices beyond Neo 2 / Intiface (focus stays Neo 2 family)  
- Windows native OpenCV CSRT via vcpkg  
- Exhaustive ROI YOLO training productization  

See `docs/FUNGEN_FEATURE_COMPARE.md`, `docs/ENGINE.md`.
