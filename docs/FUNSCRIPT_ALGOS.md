# Funscript-Algorithmen in SamNPlayer

Research-Überblick (Optical Flow, YOLO, Audio, Deep Learning, Filter-Pipelines)
übersetzt in **das, was SamNPlayer tatsächlich macht** — und was bewusst
nicht.

Stand: September 2026. Kein FunGen-Quellcode; nur öffentlich beschriebene
Ideen und eigene Messungen.

## Architektur-Regel

1. **Klassisches Tracking schreibt das Funscript** (CSRT / Flow / Grid-LK / Tf-Tj).
2. **KI schlägt nur ROI / Meinung vor** — nie die Positions-Kurve.
3. **Post-Processing** (Savgol, Detrend, Bandpass, Peaks, RDP, Speed-Cap)
   sitzt in `generator/posttrack` (Go) und spiegelt sich in
   `generate_funscript.py`.

## Empfohlener Produkt-Workflow (GUI)

**GUI tracking method = CSRT only** (Go path, portable, no Python).
Research backends (`flow`, `grid_lk`, `region_fusion*`) stay **CLI `--backend`**
until they beat CSRT on golden clips (`docs/SELF_BUILD.md`).

| Stufe | Was | Warum |
|-------|-----|--------|
| 1. ROI | Manuell oder KI-Vorschlag (verify!) | Fokus auf die relevante Bewegung |
| 2. Track | CSRT (GUI) / Tf/Tj with 2+ regions | Robuste Kurve, Go-native |
| 3. Autotune | Profil `autotune` (optional) | Detrend + Bandpass + Speed |
| 4. Check | Quality Doctor + optional Audio | Drift, Tempo, Geräte-Sicherheit |

CLI research (not the product default):

```bash
# Optional Flow draft (research only)
python3 generator/generate_funscript.py VIDEO --backend flow --flow-downscale 0.5 -o draft.funscript

# Product path: CSRT + Autotune
python3 generator/generate_funscript.py VIDEO --roi x,y,w,h --profile autotune -o out.funscript
```

## Was wir aus der Research übernehmen

| Idee | Umsetzung in SamNPlayer |
|------|-------------------------|
| Optical-Flow + Downsample | `flow` + `--flow-downscale` |
| Savitzky–Golay | `--smooth-window` / `posttrack` |
| Detrend (Drift) | `--detrend-ms` / `DetrendWindowMs` |
| Bandpass ~0.5–4 Hz | `--bandpass-hz LOW,HIGH` |
| RDP Keyframe-Reduktion | `--rdp-tolerance` |
| Speed Limiter | `--max-speed` / GUI Max. Speed |
| Ultimate-Autotune-Rezept | Profil `autotune` |
| Multi-Modal | GUI: CSRT → Autotune → Check; CLI may scout with `flow` |

## Was wir bewusst nicht kopieren

| Idee | Warum nicht (jetzt) |
|------|---------------------|
| YOLO-Körperteil-Tracking als Script-Writer | Lizenz/Scope; KI bleibt ROI-Hilfe |
| End-to-End DeepFunGen | Braucht große Datensätze; Qualität unkontrolliert |
| NVOFA-Hardware-Flow | Optional später; Farneback+Downscale deckt den Win |
| Funscript-Flow „Divergence Center“ | Bei uns schlechter gemessen (siehe HANDOFF) |

## Post-Processing-Reihenfolge

```
roh (px)
  → Savitzky–Golay
  → Rolling Detrend          (optional / autotune)
  → Bandpass 0.5–4 Hz        (optional / autotune)
  → Dynamic-Range-Lift       (optional)
  → Perzentil-Normalisierung 0–100
  → Peaks/Valleys + Prominence
  → Adaptive Keyframes + Min-Interval
  → RDP
  → Speed-Cap                (optional / autotune 400)
  → .funscript
```

## Mess-Hinweis

Jede neue Filterstufe braucht einen Vorher/Nachher-Lauf auf demselben Clip
(`Quality Doctor`-Score + Sichtprüfung der Heatmap). Autotune ist ein
**Rezept**, kein Garant — bei weichem Gewebe weiter Profil `weich` nutzen.
