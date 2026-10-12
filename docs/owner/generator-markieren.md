# Generator — Markieren (Ignore / Scene map)

**Für Owner** · Stand: **2026-09-28** · GUI-Texte Englisch, Erklärung Deutsch  
Verwandt: [`anleitung-funktionen.md`](anleitung-funktionen.md) §3 / §8 · [`zusammenhang-und-testen.md`](zusammenhang-und-testen.md) · Plan [`../SCENE_MAP_PLAN.md`](../SCENE_MAP_PLAN.md)

**Erkennungsbasis (Owner):** Everyday = **Go-CSRT (nicht-KI)** am Tip. Hybrid-KI (Scene2, Teachers, Suggest, YOLO-Review) hilft nur — Apply nötig, schreibt die Kurve nicht.

**Kurz:** Marken steuern, **welche Bildregionen** Create für Erkennung nutzen oder aussperren. Sie schreiben **nicht** die 0–100-Stroke-Kurve.

<img alt="Scene map Mark-UI (Layout)" src="media/generator-mark-ui.png" />

---

## 1. Wo sitzen die Knöpfe?

Zwei Einstiege — gleiches Ziel „Ignore (schwarz)“:

| Ort | Pfad | Wann sichtbar |
|-----|------|----------------|
| **Schnell** | Create → **2 · Where** → **+ Ignore region (black)** | Immer (neben Contact-Marken) |
| **Mit Heatmap** | Create → **4 · Create** → **Advanced settings** → **Show scene map** → Block **Mark** | Nach **Show scene map** (Video muss geladen sein) |

Weitere Mark-Arten nur unter Scene map:

- **Ignore / black** — nicht für Erkennung (Knie, Oberschenkel, Hintergrund)  
- **Source** — „Stroke ist hier“ (opt-in Hinweis für Rhythm-Grid)  
- **Region** — Körperteil-Label (Training / Review)

Contact-Marken (gold/magenta) unter Step 2 sind **Feel** (Vib-Ort), keine Scene-Map-Ignore.

---

## 2. Schritt für Schritt — Ignore malen (Scene map)

1. Video in Create wählen (Step 1).  
2. **Advanced settings** aufklappen → **Show scene map**.  
3. Kurze Heatmap läuft (~6 Fenster × ~8 s) — Fortschritt in der Statuszeile.  
4. **Map window**-Slider: Fenster wählen, in dem die falsche Erkennung sitzt.  
5. Optional **Show heatmap overlay** anlassen (teal/gold) — Ignore bleibt auch sichtbar, wenn Overlay aus.  
6. Mark-Dropdown: **Ignore / black (not for recognition)**.  
7. **Stay fixed** meist **aus** (Default): Create trackt die Box mit dem Subject („→follow“).  
   Nur anhaken, wenn etwas **statisch** im Bild bleibt (Wasserzeichen, feste Ecke).  
8. **Paint mark** → auf dem Preview eine Box ziehen (schwarz gestrichelt).  
9. Statuszeile unter den Knöpfen zeigt z. B. `1 ignore →follow`.  
10. Weitere Boxen gleich; **Clear marks** löscht alle Scene-Map-Marken dieser Session.  
11. Danach normal **Create Emotion Script** — Engine nutzt Ignore beim Rhythm-/Camera-Pfad (Follow-Path wird beim Create gefüllt).

**Schnellweg ohne Heatmap:** Step 2 → **+ Ignore region (black)** → Box malen. Gleicher Exclude-Job; Follow default an.

---

## 3. Follow vs. Stay fixed

| Modus | Checkbox | Verhalten |
|-------|----------|-----------|
| **Follow** (Default Ignore/Source) | Stay fixed **aus** | Neben-CSRT beim Create; `Path`-Samples; Preview beim Scrubben folgt |
| **Stay fixed** | Stay fixed **an** | Box bleibt wo gemalt (statischer Hintergrund) |

Nach Create: Companion-`.samn` speichert Marken inkl. Follow/Path. Beim erneuten Laden des Clips werden sie wiederhergestellt (Preview folgt beim Time/Frame-Scrub).

---

## 4. Learning (optional)

Nur wenn Settings → **Collect learning data** an:

1. Nach sinnvollen Ignore-Marken → Scene map → **Export for learning**.  
2. Später **Suggest ignores from learning** (braucht ≥3 Clips mit Ignore-Exports) — nur Vorschlag, nie auto-Create.  
3. **Clear marks** entfernt Vorschläge wieder.

---

## 5. Was Markieren **nicht** tut

- Keine Stroke-Kurve 0–100 schreiben — Everyday-Erkennung bleibt **Go-CSRT**; KI nur Assist.  
- Contact gold/magenta ≠ Ignore schwarz.  
- **Repair / Improve** (Fill gaps · Heal tracking gaps) ist ein **anderer** Schritt (klassisches Nachpolieren der Kurve, kein Re-Track, keine KI) — Anleitung §11 · [`media/generator-repair-improve.png`](media/generator-repair-improve.png).  
- **Cancelled** Virtual Person / #264 (geschlossen, out of scope).

---

## 6. Kurzer Smoke

1. Clip mit Drift auf Knie → Scene map → Ignore auf Knie → Stay fixed aus → Create.  
2. Scrub Time/Frame: schwarze Box wandert (Follow).  
3. Heatmap aus: Ignore-Box bleibt sichtbar.  
4. Optional: Collect an → Export → zweiter Clip Suggest.
