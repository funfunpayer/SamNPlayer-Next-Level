# Benchmark-Clip-Prep — Markieren · ffmpeg · kurze Clips

**Für Owner (funfunpayer)** · Stand: 2026-09-28  
**Scope:** Persönliche Benchmark-/Training-Vorbereitung — **kein** dauerhaftes Everyday-Create-UI. Everyday-Erkennung bleibt **Go CSRT**.

Repo-Tools: `scripts/benchmark-prep/` · Verwandt: [`benchmark-system.md`](benchmark-system.md) · Repo `docs/GOLDEN_CLIPS.md` · `generator/testdata/benchmark/`

---

## 1. Ziel

Lange Quellen → **kurze, verkleinerte** Clips, die:

1. schnell durch Everyday Create (CSRT) und den Bench-Tab laufen,
2. für KI-Labels / Training noch genug Detail am Tip/Kontakt haben,
3. lokal bleiben (nicht committen, nicht hochladen).

---

## 2. Bereiche markieren (In/Out)

**In der GUI (Bench-Tab):** Clip-Prep → Source wählen → **In** / **Out**
(Uhrzeit `MM:SS` / `HH:MM:SS` oder Sekunden) → Breite (Default **720p /
1280**) → **Export clip**. Optional **Use in Compare**, damit der Kurzclip
direkt im Pair-Score landet. Skript-/ffmpeg-Fallback bleibt unter
„Script / copy-paste fallback“.

Alternativ ohne GUI:

1. Langes Video in einem Player (oder SamNPlayer) scrubben.
2. **Start** und **Ende** notieren (Uhrzeit `MM:SS` / `HH:MM:SS` oder Sekunden).
3. In `scripts/benchmark-prep/example_marks.json` kopieren → `source` + `clips[]` füllen.
4. Export-Skript laufen lassen (unten).

Pro Clip sinnvoll:

| Feld | Beispiel |
|------|----------|
| `name` | `hub_easy_30s` |
| `start` / `end` | `"00:01:10"` / `"00:01:40"` |
| oder `start_sec` + `duration_sec` | `185.0` + `45.0` |

**Dauer:** typisch **20–60 s** (Golden `clip_ausschnitt` ≈ 50 s). Länger nur, wenn Drift/Schnitte geprüft werden sollen (`clip_voll`-Klasse).

---

## 3. Empfohlene Auflösung / Bitrate

| Einstellung | Empfehlung | Warum |
|-------------|------------|--------|
| **Standard** | max. Breite **1280** (`--preset-res 720p`) | Entspricht den gemessenen Golden-Clips (~1280×720); CSRT-Tip-Lock bleibt brauchbar |
| **Leicht** | max. Breite **960** (`--preset-res 960w`) | Weniger Disk/CPU, wenn Tip noch klar erkennbar |
| **Obergrenze** | 1920 (wie Soft-Proxy) | Nur wenn Quelle sehr scharf und Tip winzig — meist unnötig für Bench |
| **Nicht** | ≪720p / starke Schrumpfung | Sehr niedrige Proxies (z. B. 256×144) waren für Tracking-Arbeit zu schwach |
| Aspect | **beibehalten** (`scale=…:-2`) | Kein Stretch; gerade Höhe für yuv420p |
| Upscale | **nie** | Gleiche Regel wie `videox/playable.go` (`min(W,iw)`) |
| Codec | H.264, **CRF 20**, preset `veryfast` | An Soft-Proxy angelehnt; gut genug für CSRT + Labels |
| Audio | AAC ~128k (Default an) | Optional für Audio-Check; mit `--no-audio` weglassen |

**Kurz:** Für Training + Everyday-CSRT-Recognition **720p-Klasse (1280 breit)** als Default; **960 breit**, wenn Dateien kleiner sein sollen.

Filter (Skript + manuell identisch zur Proxy-Idee):

```text
scale='min(1280,iw)':-2:flags=lanczos
```

---

## 4. Copy-Paste — Skript

```bash
# Einzelclip
./scripts/benchmark-prep/cut_clip.sh \
  -i /pfad/lang.mp4 \
  --start 01:20 --end 02:05 \
  -o ~/clips/hub_easy.mp4

# Batch aus Marks
./scripts/benchmark-prep/cut_clip.sh \
  --marks ~/clips/marks.json \
  --out-dir ~/clips/out

# Leichtere Variante
./scripts/benchmark-prep/cut_clip.sh \
  -i /pfad/lang.mp4 --start-sec 80 --duration 45 \
  --preset-res 960w -o ~/clips/hub_easy_960.mp4
```

Windows (Python + ffmpeg auf PATH / Portable-Tools):

```powershell
python scripts\benchmark-prep\cut_clip.py -i D:\vid\long.mp4 --start 01:20 --end 02:05 -o D:\clips\hub_easy.mp4
```

Tests: `python3 scripts/benchmark-prep/cut_clip_test.py`

---

## 5. Copy-Paste — reines ffmpeg

```bash
ffmpeg -y -ss 80 -i lang.mp4 -t 45 \
  -vf "scale='min(1280,iw)':-2:flags=lanczos" \
  -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \
  -c:a aac -b:a 128k -movflags +faststart \
  hub_easy.mp4
```

Herkunft der Encode-Defaults: Soft-Proxy in `videox/playable.go` (libx264 / CRF 20 / Lanczos-Downscale, kein Upscale) — hier auf Bench-Breite 1280 statt Proxy-Cap 1920.

---

## 6. Wie Clips in Benchmark + KI-Labels fließen

```text
Mark In/Out
  → cut_clip (ffmpeg cut+scale)
  → kurzer Clip lokal
       ├─ FunGen auf denselben Cut → Referenz-.funscript
       ├─ Everyday Create (Go CSRT) → Kandidat-.funscript
       └─ Bench: Compare scripts  ODER  CLI benchmark --labels-out
            → Label good | review | bad  (JSONL für KI)
```

**Compare-only (ohne Generate):**

```bash
go run ./cmd/cli benchmark \
  --reference REF.funscript \
  --candidate CAND.funscript \
  --video ~/clips/hub_easy.mp4 \
  --json --labels-out ~/clips/labels.jsonl
```

GUI: Tab **Bench** → Video (Kurzclip) → **Suggest beside video** (füllt Ref+Kandidat aus Namenskonvention) → **Score vs reference** → **Save label for KI**.
Oder manuell: Compare scripts (Video optional für Labels).

**Golden-Manifest:** Clip-Pfad + ROI + `reference_funscript` + `backend: csrt` in Manifest (Vorlage: `generator/testdata/benchmark/example_manifest.json`) → Bench „Golden-clip manifest“ oder `golden_clip_benchmark.py`.

Schema Labels: `generator/testdata/benchmark/labels.schema.json`.  
Architecture: Everyday CSRT = Basis; FunGen = Referenz; KI assistiert/lernt aus Labels — ersetzt Everyday nicht ([`benchmark-system.md`](benchmark-system.md)).

---

## 7. Was bewusst nicht gemacht wird

- Everyday-Create-Pfad unangetastet.
- Kein schweres Mark-UI (Skript + Marks-JSON reichen für Owner-Prep).
- Virtual Person / #264 (cancelled / out of scope) / Rel38-Release-Track nicht blockieren.
- Keine Clips ins Git.
- Batch-Marks-JSON bleibt Skript-Pfad (GUI = Einzelclip In/Out).

---

## 8. Kurzer Smoke

1. `go test ./videox/ -run ClipPrep`
2. `go test ./cmd/gui-wails/ -run BenchmarkClip`
3. `python3 cmd/gui-wails/frontend/test/benchmark_clip_prep_test.py`
4. Oder: `python3 scripts/benchmark-prep/cut_clip_test.py`
5. Einen 30-s-Clip bei 1280 exportieren → in Create CSRT → Funscript.
6. Bench Compare gegen FunGen-Ref → Label speichern.
