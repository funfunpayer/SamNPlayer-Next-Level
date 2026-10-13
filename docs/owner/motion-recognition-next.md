# Bewegungserkennung — nächste Schritte (priorisiert)

**Stand:** 2026-09-28 · Nur **Motion / Tracking**-Verbesserungen  
**Basis:** Everyday **Go CSRT** (`generator/trackcv`) — hybrid KI assistiert nur.  
**Nicht:** Full-GoCV-Rewrite, KCF-Default, Whisper, #264/VP (cancelled), Rel38-Blocker.

Repo-Kontext: [`EVERYDAY_GENERATE.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/EVERYDAY_GENERATE.md) · Drift-Historie in `AGENT_COORD` / `trackcv` (dispguard, appearance_memory).  
Lern-Refs (nicht Deps): [gocv.io](https://gocv.io/) · [Capture-Artikel](https://dev.to/oleg_sydorov/gocv-opencv-an-approach-to-capture-video-329o) — siehe [`learning-repos-motion-video.md`](./learning-repos-motion-video.md).

---

## Top 5 (jetzt sinnvoll)

| Prio | Schritt | Warum / Gate | Schon da |
|------|---------|--------------|----------|
| **1** | **Long-Clip-Drift messen** auf Owner-Clips mit aktuellem Go CSRT (+ optional Rhythm-robust / Contact-points) | Residual Drift ist bekannt (`clip_voll`); erst Zahlen, dann Code. Claude/Owner-Messung, kein Blind-Rewrite | `rhythm_grid`, Contact-points Advanced; IdLock gemerged |
| **2** | **Benchmark-Kurzclips** für Tip-CSRT vs FunGen-Refs | Ohne stabile Clips keine Motion-Regression-Gate; ClipPrep parallel ([`benchmark-clip-prep.md`](./benchmark-clip-prep.md)) | Bench-Tab / golden tools |
| **3** | **Appearance-reacquire / coast** auf Hard-Tip-ROIs härten | Drift-Mechanismus #2 war falsches Reacquire; Code existiert — gezielt gegen Goldens ziehen | `appearance_memory.go`, `trackutil.Coast`, `dispguard` |
| **4** | **Auto-ROI / Tip-Find** Qualität vs manueller Box (Go-Pfad) | Everyday braucht guten Tip-Seed; Go-Parity zu Python wo noch Lücken | Classic motion candidates + optional ONNX propose |
| **5** | **Hybrid nur Assist:** ONNX/VLM-Vorschläge → Apply → gleiche CSRT-Kurve | Verbessert *Startbox*, nicht den Writer; kein KI-first Everyday | `AI_ADAPTER`, Apply-AI bereits Produkt |

---

## Explizit zurückstellen

- Full **GoCV** einziehen oder Capture-Loop aus dem DEV-Artikel 1:1 — wir haben `trackcv` + `videox`
- **KCF/MOSSE** als Everyday-Ersatz (gemessen collapsed)
- Fusion CSRT+Flow als Default (Fusion schlägt nie den Besten)
- Webcam-Capture Everyday
- Whisper / Transkription als Motion-Hilfe

Map: [`tech-stack-map-samn.md`](./tech-stack-map-samn.md).
