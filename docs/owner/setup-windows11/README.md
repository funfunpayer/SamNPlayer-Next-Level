# Windows 11 — lokales AI-Setup für SamNPlayer

**Skripte liegen im Repo unter** [`scripts/setup-windows11/`](../../../scripts/setup-windows11/) (`setup-ai-windows11.bat` / `.ps1` + kanonische README). Diese Seite ist die Owner-Kopie der Anleitung unter `docs/owner/`.

Owner-Skript: **Ollama**, **Colibri**, **Teachers** (NudeNet / onnxruntime) und **RF-DETR-Trainings-venv** in einem Lauf. Everyday Create bleibt **Go CSRT** (portables Zip mit OpenCV; ab [#339](https://github.com/funfunpayer/SamNPlayer/pull/339) wird „Region nach Cut neu finden“ auf Go-Builds soft-ignoriert). Pip-OpenCV ist nur noch für Bootstrap / PreferPython / seltene Python-Pfade nötig.

Quellen im Repo: `docs/LOCAL_MODEL_SETUP.md`, `docs/COLIBRI_SETUP.md`, `docs/VLM_MODELS.md`, `docs/WINDOWS_OPENCV.md`, `generator/ai_setup.py`. Self-build-Regel (`docs/SELF_BUILD.md`): Everyday-Qualität nicht schwächen — deshalb Training bewusst in einem **eigenen venv**.

---

## Schnellstart

Im Explorer Ordner öffnen oder in PowerShell:

```powershell
cd D:\src\SamNPlayer\scripts\setup-windows11
.\setup-ai-windows11.bat -RepoRoot D:\src\SamNPlayer -Yes
```

Nur prüfen (nichts installieren):

```powershell
.\setup-ai-windows11.ps1 -CheckOnly -RepoRoot D:\src\SamNPlayer
```

`-RepoRoot` zeigt auf einen Clone mit `generator\ai_setup.py`. Fehlt der Parameter, sucht das Skript u. a. unter `~\SamNPlayer`, `~\src\SamNPlayer` und neben `scripts\`.

---

## Was wird installiert? (Reihenfolge)

| Schritt | Was | Warum |
|--------|-----|--------|
| 1 | **Python 3.12** (winget `Python.Python.3.12`, nicht Store-Stub) | `ai_setup.py`, Teachers, Colibri-Launcher |
| 2 | **ffmpeg** (winget) | Frame-Export, AV1, Audio; Portable-Zip hat oft schon eines |
| 3 | GPU-Hinweis (`nvidia-smi`) | CUDA für Train + `onnxruntime-gpu` |
| 4 | **Ollama** (winget) + Server-Start | Qwen-VLM-Teachers (`qwen2.5vl:7b` / `qwen3-vl:8b` je VRAM) |
| 5 | **Colibri**-Prebuilt-Zip → `%LOCALAPPDATA%\SamNPlayer\colibri` | Prosa: Profil-Suggest / Qualitäts-Zweitmeinung; optional Vision-Teacher |
| 6 | **opencv-contrib-python** auf App-Python | Nur Python-Pfad (Video-Bootstrap, PreferPython). **Nicht** für Everyday Go CSRT |
| 7 | `ai_setup.py install teachers --yes` | `onnxruntime(-gpu)` + `nudenet --no-deps` (kein Ersetzen von contrib) |
| 8 | `ai_setup.py install train --yes` | Eigenes venv `%LOCALAPPDATA%\SamNPlayer\ai-train-venv` mit PyTorch (CUDA 12.8) + `rfdetr[train]` |
| 9 | `ai_setup.py install models --yes` | `ollama pull` der empfohlenen Vision-Modelle ab ~10 GB VRAM |

Überspringen: `-SkipOllama`, `-SkipColibri`, `-SkipTeachers`, `-SkipTrain`, `-SkipOpenCvContrib`.

**Nicht** heruntergeladen: Colibri-**Modellgewichte** (oft hunderte GB auf schneller NVMe). Engine-Zip ja, Snapshot selbst beschaffen und `COLI_MODEL` setzen.

---

## GPU / Hardware

| Ziel | Hinweis |
|------|---------|
| NudeNet + RF-DETR Nano/Small | NVIDIA ab ~8–16 GB VRAM reicht zum Start |
| Qwen2.5-VL 7B / Qwen3-VL 8B (Ollama) | ca. 10+ GB VRAM (Q4) |
| Qwen2.5-VL 32B | eher 24 GB |
| Colibri große MoE | vor allem **RAM + NVMe**, nicht GPU; 64–128 GB RAM, 1–2 TB frei |
| Ohne NVIDIA | Teachers auf CPU (`onnxruntime`); Train-venv ohne CUDA-Index — sehr langsam |

Treiber: aktuelle NVIDIA Studio/Game-Ready. Train-venv nutzt PyTorch-Wheels von `download.pytorch.org/whl/cu128` (wie `ai_setup.py`).

---

## Training nach dem Setup

1. Teachers → Kontaktpunkte (GUI Create → Advanced → Rhythm-robust, oder CLI `contact_points.py`).
2. Kandidaten im SceneMap prüfen (`reviewed:true`).
3. Dataset:  
   `…\ai-train-venv\Scripts\python.exe generator\contact_detector.py dataset --learning-dir … --out ds`
4. Train:  
   `…\ai-train-venv\Scripts\python.exe generator\contact_detector.py train --dataset ds --out contact_detector.onnx --device cuda`
5. Eigenes ONNX wieder als Teacher: `contact_points.py --onnx contact_detector.onnx`

**Wichtig:** Training immer mit dem **Train-venv**-Python — RF-DETR zieht `opencv-python` und darf nicht neben App-`opencv-contrib` liegen (CSRT).

ROI-/YOLO-Training (älterer Pfad): GUI **AI training** → Install AI train deps / Start training; Details `docs/KI_TRAINING.md`.

---

## Colibri starten

```powershell
cd $env:LOCALAPPDATA\SamNPlayer\colibri
$env:COLI_MODEL = "D:\path\to\converted\model"
py .\coli serve --host 127.0.0.1 --port 8080
```

- SamNPlayer Settings → AI-Server leer = `http://127.0.0.1:8080` → **Test AI server**.
- Vision-Teacher-Preset in `contact_points.py` erwartet oft Port **8000** — denselben Port wählen, den du startest (`docs/COLIBRI_SETUP.md`).

---

## Verifizieren

```powershell
.\setup-ai-windows11.ps1 -CheckOnly -RepoRoot D:\src\SamNPlayer
# oder:
py D:\src\SamNPlayer\generator\ai_setup.py check
```

Erwartung grob:

- GPU-Zeile oder ehrlicher „none found“
- `opencv_contrib` / `nudenet` / `onnxruntime` ok (Teachers)
- `train venv` ok unter `%LOCALAPPDATA%\SamNPlayer\ai-train-venv` (+ CUDA wenn NVIDIA)
- `ollama` up + passende Vision-Tags
- Colibri: `colibri.exe` vorhanden; „up“ erst nachdem `coli serve` läuft

Everyday Generate (portables Release): Log **`Go-native CSRT pipeline`** — nicht „CSRT not available in OpenCV 5…“ (das war der Python-Pfad vor #339).

---

## Schalter (Kurz)

| Parameter | Bedeutung |
|-----------|-----------|
| `-RepoRoot PATH` | SamNPlayer-Checkout |
| `-Yes` | ohne Nachfrage installieren |
| `-CheckOnly` | nur Status |
| `-ColibriDir PATH` | anderes Colibri-Ziel |
| `-ColibriPort N` | Hinweisport in der Ausgabe (Default 8080) |
| `-Skip*` | einzelne Schritte auslassen |

---

## Abgrenzung

| Nicht Aufgabe dieses Skripts | Wo |
|------------------------------|-----|
| Everyday-Tracking | Go CSRT im Release-Binary |
| Funscript-Kurve schreiben | Create / CSRT |
| Colibri-Gewichte pullen | manuell / Upstream-Docs |
| Cursor-Cloud-Agent-Modell | irrelevant |

Bei Problemen zuerst `ai_setup.py check` und `docs/LOCAL_MODEL_SETUP.md` Abschnitt G.
