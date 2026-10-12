# Lern-Repos — Motion / Video / Desktop (SamNPlayer)

**Stand:** 2026-09-28 · Audience: Owner (DE) + Agents  
**Quelle:** Owner-Paste ähnlicher Go/Wails/FFmpeg/AI-Repos + GoCV-Links  
**Regel:** Everyday = **Go CSRT** (`generator/trackcv`). Hybrid KI nur Assist/Verify. Keine neuen Schwer-Deps ohne Owner-Nutzen.

Repo-Filter bereits dokumentiert: [`docs/ENGINEERING_STANCE.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/ENGINEERING_STANCE.md) § „Go / video / CV link dump“.

---

## Top zum Lernen (Steal-Ideen, nicht Deps)

| # | Repo | Was stehlen / lernen | SamN-Bezug |
|---|------|----------------------|------------|
| **1** | [ericfisherdev/GoByeBye](https://github.com/ericfisherdev/GoByeBye) | ONNX Runtime laden, Session, GPU/CPU-Batch — saubere lokale Inferenz-Patterns | Optional später Go-side ONNX; heute ROI via Python `onnxruntime` ([`docs/AI_ADAPTER.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/AI_ADAPTER.md)) |
| **2** | [farshidrezaei/vidonex](https://github.com/farshidrezaei/vidonex) | Große FFmpeg-/Filtergraph-Pipeline in einer Desktop-App orchestrieren | Ideen für `videox` / Proxy / Cut — **nicht** Vidonex als Dep ([`docs/FFMPEG_TOOLS.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/FFMPEG_TOOLS.md)) |
| **3** | [ZioSHik/kinopub-gui](https://github.com/ZioSHik/kinopub-gui) | Portable FFmpeg + Progress/State für lange Jobs + Video-Player-UX | Passt zu Portable-Zip + „Make playable“ + Generate-Progress |

**Weitere brauchbar (kurz):**

| Repo | 1–2 Sätze |
|------|-----------|
| [arnoldhao/dreamcreator](https://github.com/arnoldhao/dreamcreator) | Wails + lokale AI + Video-Tools in einem Produkt — UI-/Workflow-Muster für opt-in KI, nicht Everyday-Replace. |
| [MathisVerstrepen/spritely](https://github.com/MathisVerstrepen/spritely) | Wails + Video-Automation — Frame-/Batch-Orchestrierung neben Generate. |
| [deadelus/live-semantic](https://github.com/deadelus/live-semantic) | GoCV CSRT/KCF Tracker-Code zum Lesen — Muster vergleichen mit unserem `trackcv`, **kein** GoCV-Import. |

---

## GoCV / OpenCV — Referenz (lernen, nicht Stack umbauen)

SamNPlayer hat Everyday-Tracking bereits: **Go CSRT** über dünnes CGO `generator/trackcv` (nicht Full-[GoCV](https://gocv.io/) als Modul). KCF als Everyday-Ersatz wurde gemessen und verworfen ([`docs/NEXT.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/NEXT.md), [`docs/WINDOWS_OPENCV.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/WINDOWS_OPENCV.md)).

| Link | Nutzen für uns |
|------|----------------|
| [gocv.io](https://gocv.io/) | Offizielle GoCV-Doku — API-Ideen (Tracker, Capture, Mat-Lebenszyklus). **Nicht** `go get gocv` in SamN; wir bleiben bei `trackcv`. |
| [GoCV + OpenCV: approach to capture video (DEV)](https://dev.to/oleg_sydorov/gocv-opencv-an-approach-to-capture-video-329o) | Praktischer Capture-/Frame-Loop — vergleichen mit unserem Decode-Pfad (`videox` + OpenCV in `trackcv`). Produkt = **Datei → Script**, kein Webcam-Everyday. |

Siehe Map: [`tech-stack-map-samn.md`](./tech-stack-map-samn.md).

---

## Bewusst übersprungen / schwach für SamN

| Paste-Eintrag | Warum skip |
|---------------|------------|
| WhiteVPN-Desktop, TDrive, go-wind-toolkit, linkit, Nipah--Anime | Wails-Architektur ok, aber wenig Motion/Video/Funscript-Nutzen |
| scribe-studio, video-transcriber (+ Whisper.cpp) | Transkription ≠ Stroke; Audio bei uns = Tempo-Check ([`docs/AUDIO_WORKFLOW.md`](https://github.com/funfunpayer/SamNPlayer/blob/main/docs/AUDIO_WORKFLOW.md)), kein Whisper |
| Ebiten / raylib / Gio als Motion-Stack | Spiele-/UI-Motion ≠ Tip-Tracking |
| Full GoCV + KCF als Default | Widerspricht Stance + Messungen; Everyday bleibt CSRT/`trackcv` |
| Quick-Start „alles neu installieren“ | Stack existiert — siehe Map |

---

## Lernreihenfolge (Owner, 30–90 Min Stücke)

1. Eigenes Repo: `EVERYDAY_GENERATE.md` → `WINDOWS_OPENCV.md` → `FFMPEG_TOOLS.md` → `ENGINEERING_STANCE.md` Filter  
2. [gocv.io](https://gocv.io/) + [DEV Capture-Artikel](https://dev.to/oleg_sydorov/gocv-opencv-an-approach-to-capture-video-329o) — nur lesen, gegen `trackcv` spiegeln  
3. GoByeBye → ONNX-Patterns (optional KI)  
4. kinopub-gui → FFmpeg/portable State  
5. vidonex → Pipeline-Architektur (Master-Niveau, Ideen only)

Motion-Next: [`motion-recognition-next.md`](./motion-recognition-next.md).
