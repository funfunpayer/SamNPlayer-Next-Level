# Benchmark-System — Design (Owner)

**Für Owner (funfunpayer)** · Stand: 2026-09-28  
**Nordstern:** Everyday-Erkennung (Go CSRT) ist **immer** die Basis — auch wenn KI Skripte schreibt und **besonders** im Hybrid (KI hilft/prüft, ersetzt die Basis nicht). Benchmark bewertet gegen diese Everyday-Basis **plus** FunGen-Referenzen. Kein KI-first Everyday.

Verwandt im Repo: `docs/GOLDEN_CLIPS.md`, `docs/KI_TRAINING.md`, `docs/SIGNAL_VS_FIDELITY.md`, `generator/golden_clip_benchmark.py`, `generator/fungen_compare.py`, CLI `phase` / `compare` / `benchmark`.

---

## 1. Was der Benchmark entscheiden soll

**Frage:** Ist ein erzeugtes Skript **gut**, **prüfen** oder **nicht gut** — gemessen an FunGen-Referenz **und** Quality Doctor, mit Everyday als Produktionsbasis.

| Label | Deutsch | Bedeutung |
|-------|---------|-----------|
| `good` | **gut** | Motion Fidelity vs. Referenz ok (`verdict=ok`, r ≥ 0,70, keine low confidence) **und** Quality Doctor bestanden |
| `review` | **prüfen** | z. B. Timing/Phase (Kurve passt nach Lag) oder Quality Doctor schwächelt |
| `bad` | **nicht gut** | Form/Wahrnehmung schwach, keine Korrelation, oder harter Fail |

`passed=true` nur bei `good`. Schwellen kommen aus dem bestehenden Engine-Code (`DefaultMinAlignedR=0.70`, Quality Doctor), nicht neu erfunden.

Zwei getrennte Messgrößen (nicht vermischen):

1. **Motion Fidelity** — Kandidat vs. FunGen/Referenz (Lag-Korrelation, Orientierung)
2. **Signal Quality** — Quality Doctor nur auf dem Kandidaten (ohne Video)

---

## 2. Owner-Flow (MVP)

```text
Video wählen (optional, für Labels)
    → Referenz-FunScript(s) laden (FunGen)
    → Kandidat: Everyday Create / geladenes Skript
         (Hybrid: KI darf assistieren — Score bleibt vs. Everyday-Basis + Ref)
    → Score → gut / prüfen / nicht gut + Metriken
    → optional: Label für KI speichern (JSONL)
```

**GUI (Tab Bench):**

1. **Suggest beside video** — nach Clip-Prep: Video wählen → Button füllt `stem.funscript` (FunGen/Ref) + `stem__hub.funscript` (Everyday-Kandidat) aus dem Clip-Ordner (inkl. Unterordner wie `mit_yolo/`). Nur `stem.funscript`? → als Kandidat; Ref per Browse.
2. **Compare scripts** — Video (für Labels) + Referenz + Kandidat → **Score vs reference** → Anzeige GUT/PRÜFEN/NICHT GUT; **Save label for KI**
3. **Golden-clip manifest** — Manifest mit lokalen Clips → echte Everyday-Pipeline → Verlauf

**CLI:**

```bash
# Compare-only (kein Video nötig, wenn beide Funscripts da sind)
go run ./cmd/cli benchmark \
  --reference REF.funscript \
  --candidate CAND.funscript \
  --video /pfad/clip.mp4 \          # optional
  --json --labels-out labels.jsonl

# Alias
go run ./cmd/cli score --reference REF --candidate CAND --json
```

Exit `0` = gut, `1` = prüfen/nicht gut, `2` = Argumentfehler.

Bestehende Hooks bleiben: `phase` (roh), `compare --dataset` (Ordner FunGen×hub/tf/tj), `golden_clip_benchmark.py` (Manifest + Generate).

---

## 3. Architektur-Regel (Owner bestätigt)

| Darf | Darf nicht |
|------|------------|
| Everyday Go CSRT als Schreib-/Vergleichsbasis | KI als stiller Everyday-Ersatz |
| FunGen-`.funscript` als Referenz | FunGen-Quellcode / `.fungen` |
| Hybrid: KI schlägt vor / prüft; Score gegen Everyday+Ref | „KI-first Everyday“ erfinden |
| Labels aus Scores für späteres Training/Eval | Cloud-Upload der Clips |

Golden-Clip-Manifest setzt `backend: csrt` (Everyday). KI-Drafts können als **Kandidat** geladen werden — dann misst der Benchmark, wie nah sie an FunGen + Quality Doctor liegen; die Produktions-Everyday-Spur bleibt CSRT.

---

## 4. Anbindung Testdaten / KI

| Vorhanden | Nutzung |
|-----------|---------|
| `generator/testdata/golden_clips/clip_*` | FunGen-Refs + `__hub` Everyday-Ausgaben (Videos lokal) |
| `generator/testdata/benchmark/` | Beispiel-Pair, Manifest-Form, `labels.schema.json` |
| `vlm_oracle.json` neben Goldens | VLM-Labels (Teacher) — getrennt vom Pair-Score |
| Benchmark-History JSONL | Manifest-Läufe über Zeit |
| `benchmark_pair_labels.jsonl` | KI-Lesbare Pair-Labels (`good`/`review`/`bad` + voller Score) |

Videos bleiben auf der Owner-Maschine (`docs/GOLDEN_CLIPS.md`). Im Repo nur Funscripts + Manifest-Form + Schema.

---

## 5. Was „gut“ technisch heißt

```text
good  ⇔  fidelity.verdict == ok
      ∧  r ≥ 0.70
      ∧  !low_confidence
      ∧  quality.passed

review ⇔ timing-Verdict, low confidence, oder Quality fail bei noch brauchbarer Form
bad    ⇔ shape / undefined / sehr schwache Korrelation (r < 0.40)
```

Implementierung: `funscript.ScorePair` — baut auf `EvaluateMotionFidelity` + `EvaluateScriptQuality` (gleiche Zahlen wie `phase` / Script Doctor).

---

## 6. Abgrenzung

- Kein Eingriff in Everyday-CSRT-Default, #264/VP (cancelled / out of scope), oder Rel37-Tagging.
- Nur Bench/CLI/Funscript-Score + Testdata-Layout unter `generator/testdata/benchmark/`.

---

## 7. Später (nicht MVP)

- Nach Score: „Mit Everyday Create neu erzeugen“ und sofort gegen dieselbe Referenz scoren
- Mehrere Achsen (hub/tf/tj) in einem GUI-Lauf / Dropdown wenn mehrere Refs neben dem Clip
- Manifest-Lauf schreibt automatisch Pair-Labels pro Clip
- Fenster-Korrelation (`phase --window-ms`) in der GUI-Ansicht
- Owner-Override gut/prüfen/nicht gut vor JSONL-Save
