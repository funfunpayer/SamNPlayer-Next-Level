# Advanced Generator — UI-Audit & Aufräumen

**Produkt:** SamNPlayer Emotion GUI · Create → Schritt **4 · Create** → **Advanced settings**  
**Stand:** nach v0.5.37 · Quelle: `cmd/gui-wails/frontend/src/generator.js` (`#gen-advanced`)  
**Regel:** Everyday-Basis ist **immer** Tip → **Go CSRT** → Create. Advanced darf das nicht brechen und **nicht** Richtung KI-first Everyday schieben. Hybrid = Assist/Verify auf dieser Spine (opt-in, Review).  
**Owner-Bestätigung (28 Sep 2026):** Everyday Go CSRT = base; Hybrid nur assist/verify — kein KI-first Everyday.  
**Cancelled:** [#264](https://github.com/funfunpayer/SamNPlayer/pull/264) Virtual Person — closed, not deferred.

GUI-Texte in der App bleiben Englisch. Owner-Abschnitte hier auf Deutsch.

---

## Kurzfazit (Owner)

Advanced war **eine flache Liste**: nützliche Opt-ins (Invert, Rhythm, Scene map) neben totem Dropdown (Tracking method = nur CSRT), Doppel-Kontrolle (Impulse-Vib auch unter Feel → Curve) und Expert-Zahlen (Smoothing/RDP), die Everyday selten braucht.

**Aufräumen (PR [#347](https://github.com/funfunpayer/SamNPlayer/pull/347)):** Progressive Disclosure — klarere Gruppen, Expert-Tuning einklappen, Tracking-Method-Dropdown aus dem Blick (DOM bleibt), Impulse unter Expert, **AI draft zugeklappt** (nie Everyday-Default). Hybrid-Texte betonen Assist/Verify auf CSRT-Spine.

Everyday Create / CSRT-Go-Pfad: unverändert (Defaults, Payload-IDs, Soft-ons).

---

## Was Everyday braucht vs. Power-User

| Kategorie | Everyday | Power-User / selten |
|-----------|----------|---------------------|
| Tip → CSRT → Contact vib | Ja (Steps 1–4, Advanced zu) | — |
| Invert / Cam-comp / Scene-cut | Nur bei falschem Feel / Kamera / Cuts | Checkboxes (sinnvoll sichtbar) |
| Record tip path | Soft-on mit Contact vib | Abschalten möglich |
| Rhythm + Contact points | Nein (Default aus) | Long-Clip-Drift |
| Scene map / Marks / Learning | Nein | Ignore-Knie, Priors, P5c |
| AI draft / Export classical | Nein | **Zugeklappt** unter Advanced — Imitation, Keep nötig |
| Sliding dynamics / Auto-Retry / O-markers | Defaults ok | Sichtbar, selten ändern |
| Axis / Adaptive / Smooth / Peak / RDP / Max speed / Re-find | Defaults ok | **Expert** (eingeklappt) |
| Tracking-method Dropdown | Irrelevant (nur CSRT) | Versteckt; CLI für andere Backends |
| 4-zone | CLI only | Button bleibt `hidden` |
| Peak-emphasis impulse | Feel → Curve | Expert-Spiegel (sync) |

---

## Kontroll-Inventar (`#gen-advanced`)

### A · Tracking & Polarity (sichtbar nach Cleanup)

| ID | Label (EN) | Funktion | Everyday? | Entscheidung |
|----|------------|----------|-----------|--------------|
| `gen-invert` | Invert motion direction | Kurve 100−pos | Bei invertiertem Feel | **Behalten** sichtbar |
| `gen-camcomp` | Camera motion compensation | Kameraschwenks (Default an) | Default an | **Behalten** |
| `gen-scenecut` | Scene-cut detection | Re-Anker nach Hard-Cuts (Default an) | Default an | **Behalten** |
| `gen-capture-trajectory` | Record tip path | Tip-(x,y) für Feel Stage A / Overlay; Soft-on mit Contact vib | Soft-on ok | **Behalten** sichtbar |
| `gen-backend` | Tracking method | Nur Option `csrt` | Irrelevant | **Demote:** visuell versteckt unter Expert; Wert bleibt `csrt`; andere Backends = CLI (`docs/EVERYDAY_GENERATE.md`) |
| `gen-backend-hint` | Hint-Text | Erklärt CSRT-Tip | Redundant neben Product-Path | **Gekürzt** → eine Zeile unter Expert |

### B · Long-clip Anti-Drift (sichtbar)

| ID | Label | Funktion | Everyday? | Entscheidung |
|----|-------|----------|-----------|--------------|
| `gen-rhythm-grid` | Rhythm-robust signal | Rhythm-Zellen nah am Tip; Go-CSRT; ~+18 % Zeit | Opt-in | **Behalten**; eigene Gruppe |
| `gen-contact-points` + path/pick | Use contact points | Teachers-JSON nur bei Rhythm an; sonst bit-identisch | Opt-in | **Behalten** (progressive Show wie bisher) |
| `gen-contact-verify` | Verify with the engine (hybrid, K=1.5) | `ContactVerifyK=1.5` — Engine filtert schwache Teacher-Punkte | Opt-in | **Behalten**; nur sichtbar wenn Use an |
| `gen-cp-*` / `gen-contact-points-run` | Generate contact points | NudeNet / Ollama / LM Studio → `.contact.json` | Opt-in | **Behalten** |

### C · Scene map (eigene Gruppe — war unter „AI draft“ vermischt)

| ID | Label | Funktion | Everyday? | Entscheidung |
|----|-------|----------|-----------|--------------|
| `gen-scene-map` | Show scene map | Heatmap ohne Generate | Opt-in | **Behalten**; Gruppe **Scene map** |
| `gen-scene-map-win` / overlay / mark-* | Window, Overlay, Marks | Ignore/Source/Region, Stay fixed | Power | **Behalten** |
| `gen-scene-map-export` / suggest | Learning export / Suggest ignores | Collect learning; L1-Priors | Power | **Behalten** |
| `gen-import-contact-candidates` + Accept/Reject | Teacher candidates | `author:auto` → P5c | Power | **Behalten** |

### D · AI draft (experimental)

| ID | Label | Funktion | Everyday? | Entscheidung |
|----|-------|----------|-----------|--------------|
| `gen-ai-script-export` | Export classical run | S1-Imitations-Sample nach Create | Power | **Behalten** |
| `gen-ai-script-draft` / keep / discard | AI draft script | Streckt Sample → Review → Keep | Experiment | **Behalten**; Scene map **nicht** mehr in dieser Gruppe |

### E · Signal & quality (sichtbar)

| ID | Label | Funktion | Everyday? | Entscheidung |
|----|-------|----------|-----------|--------------|
| `gen-dynrange` | Sliding dynamics | Schwache Abschnitte anheben (Default an) | Default an | **Behalten** |
| `gen-retry` | Auto-Retry | Parameter-Retry bei schlechter QD (Default an) | Default an | **Behalten** |
| `gen-auto-ozone` | Suggest O-markers | Klassisch aus Signal, Ende hoch | Opt-in | **Behalten** |
| `gen-audio-check` | (hidden) | Audio-Tempo-Check bei Generate | Default an | **Bleibt hidden** — UI in Review (`docs/AUDIO_WORKFLOW.md`) |

### F · Expert tuning (eingeklappt nach Cleanup)

| ID | Label | Funktion | Everyday? | Entscheidung |
|----|-------|----------|-----------|--------------|
| `gen-contact-impulse` | Peak-emphasis contact vib | Setzt Curve=`impulse` | Experiment | **Demote** Expert; Primärsteuerung bleibt **Feel → Curve → Impulse** (sync unverändert) |
| `gen-axis` | Motion axis | Auto / force x|y | Default Auto | **Demote** Expert |
| `gen-adaptive` | Adaptive Keyframes | Extra KFs (Default an) | Default an | **Demote** Expert |
| `gen-perscene` | Re-find region after each cut | PerSceneROI — auf Go-CSRT-Builds **Python-only / soft-ignore** (#338/#339) | Selten | **Demote** Expert + klarer Help-Text (kein Everyday-Default) |
| `gen-smooth` / `gen-peakdist` / `gen-rdp` / `gen-maxspeed` | Smoothing, Peak spacing, RDP, Max speed | Posttrack-Zahlen | Defaults | **Demote** Expert |

### Außerhalb Advanced (zum Abgleich)

| Ort | Kontrolle | Hinweis |
|-----|-----------|---------|
| Step 3 Feel | Style, Contact vib, Sensitivity, **Curve** (inkl. Impulse) | Everyday Feel |
| Step 3 | Power-user: scene memory | Collapsed (Ballast policy) |
| Step 2 | Ignore region (black) | Gleiches Job wie Scene map → Ignore |
| Preview (`roi_help.js`) | Invert-Klon | Sync → `#gen-invert`; nicht entfernen |

### Bereits entfernt (Ballast — dokumentiert)

| Früher | Wohin |
|--------|-------|
| AI second opinion | Entfernt (kein Kurven-Effekt) — `CONTENT_SOURCES.md` Ballast |
| Flow downscale | CLI / Flow-only — GUI weg |
| Audio-check-Zeile in Advanced | Generate-Default bleibt hidden; UI = **Review → Audio check** |
| 4-zone / Flow / grid_lk Dropdown | CLI; Produkt-GUI nur CSRT |
| `#gen-nomark` (4-zone button) | **Removed** on PlayHeatUX (#460) — CLI backends kept; no GUI control |

---

## Cleanup-Umsetzung (GUI)

Datei: `cmd/gui-wails/frontend/src/generator.js` (+ kleine CSS für nested Expert).

2. Gruppen: **Tracking & polarity** · **Long-clip anti-drift (hybrid assist)** · **Scene map** · **AI assist** (zugeklapptes AI draft) · **Signal & quality** · **Expert tuning**.  
3. `#gen-backend` bleibt im DOM (`value=csrt`), sichtbar nur als Hinweis unter Expert.  
4. Impulse-Checkbox → Expert; Feel → Curve bleibt Source of Truth.  
5. Keine Payload-ID-Änderungen; Defaults unverändert; **kein KI-first Everyday**.

---

## Nicht anfassen

- Everyday Create-Button / Progress / Review Improve  
- Go-CSRT-Eligibilität / Soft-clear PerSceneROI  
- Virtual Person / Plugin-Produkt (#264 cancelled / closed)  
- Scene2 Apply / Tip-Find Steps 1–3 außer Verweistexten  

---

## Tests / Abnahme

- Frontend: bestehende Playwright-Tests (`generator_everyday_*`, `generator_backend_*`, rhythm/contact/scene map) — IDs bleiben.  
- Manuell: Advanced zu → Create; Advanced → Invert; Rhythm an → Contact points sichtbar; Expert aufklappen → Re-find/Backend-Hinweis.
