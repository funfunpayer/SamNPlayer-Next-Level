# Tech-Stack-Map — auf SamNPlayer gemappt

**Stand:** 2026-09-28 · Audience: Owner (DE)  
**Owner-Regel:** Everyday **Go CSRT** ist immer die Basis; Hybrid KI nur Assist/Verify.  
**Nicht:** „alles neu installieren“-Checkliste. Stack existiert.

Paste-Roh: Tech-Stack-Map mit GoCV/CSRT/KCF, FFmpeg, ONNX, Whisper, Wails. Hier: **was wir schon haben** vs. optionale Lücken.

---

## Kurz: Paste → Realität

| Paste-Schicht | SamNPlayer heute | Verdict |
|---------------|------------------|---------|
| GoCV CSRT/KCF | **Go CSRT** via dünnes CGO [`generator/trackcv`](https://github.com/funfunpayer/SamNPlayer/tree/main/generator/trackcv) (OpenCV contrib), **nicht** Full-Modul `gocv.io/x/gocv` | **Haben** Everyday. KCF gemessen → collapse auf Hard-ROI → **nicht** Everyday ([`NEXT.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/NEXT.md)) |
| FFmpeg | [`videox`](https://github.com/funfunpayer/SamNPlayer/tree/main/videox) + portable/tools ([`FFMPEG_TOOLS.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/FFMPEG_TOOLS.md)) | **Haben** |
| ONNX | Python `onnxruntime` ROI / Teachers / RF-DETR Train ([`AI_ADAPTER.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/AI_ADAPTER.md), [`KI_TRAINING.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/KI_TRAINING.md), [`LOCAL_MODEL_SETUP.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/LOCAL_MODEL_SETUP.md)) | **Haben** (opt-in). Go-ONNX = optionale spätere Lücke |
| Whisper.cpp | — | **Nicht nötig** — Audio = Tempo-Check, keine Transkription |
| Wails + UI | [`cmd/gui-wails`](https://github.com/funfunpayer/SamNPlayer/tree/main/cmd/gui-wails) (Vanilla JS, nicht React-Pflicht) | **Haben** |
| Python | Train / Teachers / PreferPython / Research | **Bewusst** — nicht Everyday-Writer |

Filter-Doku (älterer Link-Dump): [`ENGINEERING_STANCE.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/ENGINEERING_STANCE.md) § Go/video/CV.  
Produktpfad: [`EVERYDAY_GENERATE.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/EVERYDAY_GENERATE.md) · Windows CSRT: [`WINDOWS_OPENCV.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/WINDOWS_OPENCV.md).

---

## Referenz-Links GoCV (lernen, kein Rewrite)

| | |
|--|--|
| Offiziell | [gocv.io](https://gocv.io/) |
| Capture-Ansatz | [GoCV OpenCV: approach to capture video](https://dev.to/oleg_sydorov/gocv-opencv-an-approach-to-capture-video-329o) |

**Mapping:** Everyday = bereits Go+OpenCV+CSRT (`trackcv`). Diese Links erklären die *Idee* Capture/Tracker in Go — wir kopieren Patterns in unseren Wrapper, **installieren nicht** Full-GoCV und schreiben Everyday nicht um. Stance: Full GoCV = compile/patent pain → reject als Dep.

---

## Pipeline (Paste vs. SamN)

**Paste (vereinfacht):** Video → FFmpeg Frames → GoCV Preprocess → optional ONNX → GoCV CSRT/KCF → Script → FFmpeg Export  

**SamNPlayer (Ist):**

```text
  Video
    │
    ├─► videox (ffmpeg/ffprobe): proxy, preview, audio extract, cuts
    │
    ▼
  optional: ONNX ROI / Go profilemodel / SceneMap marks   ← Assist only
    │  Apply / user tip box required for curve path
    ▼
  Everyday: tip → Go CSRT (trackcv) [+ coast / appearance / rhythm opt-in]
    │
    ▼
  posttrack → Quality Doctor → .samn + .funscript
    │
    ▼
  Play / device (Neo 2) — not Whisper, not cloud video APIs
```

Hybrid: KI schlägt vor / verifiziert; **Kurve schreibt CSRT**.

---

## Schicht-Map

### 1 · Bewegung / Tracking

| Stück | Status | Wo |
|-------|--------|-----|
| CSRT tip Everyday | **Ist** | `trackcv` + native Generate |
| Coast / appearance reacquire / dispguard | **Ist** | `trackcv`, `trackutil` |
| Rhythm-robust signal | **Ist** (Advanced opt-in) | `rhythm_grid.go` |
| KCF / MOSSE Everyday | **Reject** (gemessen) | `NEXT.md` |
| Full GoCV Dep | **Reject** | Stance; Learn: [gocv.io](https://gocv.io/) |
| Webcam / V4L2 Everyday | **Out** | Datei→Script Produkt |

### 2 · Video / Media

| Stück | Status | Wo |
|-------|--------|-----|
| ffmpeg portable / Settings install | **Ist** | `videox`, `FFMPEG_TOOLS.md` |
| Make playable / proxy | **Ist** | `videox/playable.go` |
| Benchmark Clip-Prep | **Parallel** | [`benchmark-clip-prep.md`](./benchmark-clip-prep.md) |

### 3 · Lokale KI

| Stück | Status | Wo |
|-------|--------|-----|
| ONNX ROI propose | **Ist** opt-in | `ai_roi`, Teachers |
| Go profilemodel suggest | **Ist** | `#244` / Create labels |
| Colibri / VLM teachers | **Ist** opt-in | `COLIBRI_SETUP`, `VLM_*` |
| RF-DETR / contact train | **Ist** Train-Pfad | `KI_TRAINING`, setup-windows11 |
| Whisper | **Skip** | kein Product-Need |
| Go `yalue/onnxruntime_go` | **Optional Gap** | nur wenn Python-Hop gemessen wehtut |

### 4 · Desktop

| Stück | Status | Wo |
|-------|--------|-----|
| Wails GUI | **Ist** | `cmd/gui-wails` |
| Portable Windows OpenCV DLLs | **Ist** | `WINDOWS_OPENCV.md`, Rel portable |

---

## Optionale Lücken (klein, Owner-Nutzen nötig)

1. **Go-native ONNX** für ROI — nur nach Messung (Python-Hop vs. Qualität). Patterns: GoByeBye + [Lern-Repos](./learning-repos-motion-video.md).  
2. **Sparse seek / cancel-safe ffmpeg** neben Generate — Stance B1/B2, schon teils MT-Infra.  
3. **Bewegungserkennung verbessern** — nicht neuer Tracker-Stack: [`motion-recognition-next.md`](./motion-recognition-next.md).

**Keine Lücke:** „GoCV installieren“, „KCF Default“, „Whisper einbauen“, „React erzwingen“.
