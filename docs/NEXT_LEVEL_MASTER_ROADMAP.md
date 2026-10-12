# SamNPlayer Next Level — Engineering Masterplan

**Stand:** 12.10.2026 · **Status:** Architektur- und Entwicklungsplan, noch keine ausgelieferte Next-Level-Software.  
**Produktentscheidung:** neue eigenständige Softwaregeneration, aus dem vorhandenen SamNPlayer abgeleitet. Free/Community wird separat fertiggestellt und weitergepflegt. Bestehenden Code und bewährte Ansätze nicht unreflektiert wegwerfen. Entwicklungszeit: Monate bis Jahre; Qualität vor Geschwindigkeit.

## 1. Leitidee

Der Nutzer wählt Video, optional Ziel/ROI und Geräteprofil. **Eine eigene klassische Engine erkennt möglichst viel selbst, erstellt ein hochwertiges Master-Funscript und prüft unabhängig jede unsichere Szene.** Optional lokale KI liefert Regions-/Szenenvorschläge und erstellt oder repariert Script-Kandidaten auf Basis realer Bewegungsdaten. Der Verifier übernimmt nur nachweislich brauchbare Ergebnisse. Per-Gerät-Adaption folgt dem tatsächlichen Capability-Modell. Alle relevanten Schritte bleiben im vollwertigen Multi-Axis-Editor nachvollziehbar und umkehrbar.

**Simple by default, professional by choice:** Auto / Guided / Expert. Auto = Video → Analyse → Script → Qualitätsprüfung → Export. Guided = Warnungen/Szenen überprüfen. Expert = vollständiger Editor, Tracking-Vergleich, Multi-Axis, Profiles, Batch und Messdaten.

## 2. Architektur und Sprachwahl

```text
Wails GUI (JS/CSS; vorhandene Flow-Assets und Navigation übernehmen)
  -> Go App/API: Commands, Cancel, Jobs, Progress, Settings
      -> MediaProbe/PTS Timeline & FrameCache (FFmpeg/OpenCV)
      -> SceneScanner (klassische Bild-/Audioanalyse)
      -> TrackerRegistry
           - vorhandener CSRT/OpenCV-Tracker als bewährte Baseline
           - alternative CV-Backends als Kandidaten
           - Rust/C++ native Hotspots NUR bei gemessenem Gewinn
      -> CandidateScorer / SelectiveRetry / CurveComposer
      -> LocalAIAdapter (optional ONNX / LM Studio auf Mac)
      -> IndependentVerifier (PTS, Rohbewegung, Jitter, Gap, Device-Limits)
      -> SAM/.samn Projekt + universelles .funscript + reale Zusatzachsen
      -> EditorService (History, Transactions, Interpolation, A/B)
      -> DeviceCompiler / SafetyGovernor / Preview / DeviceAdapters
      -> Batch Job Queue (persistiert, Recovery, Report)
```

**Go:** Orchestrierung, API, Dateiformate, Jobs, Device-Governor, Editoroperationen und Prüfberichte. **Rust:** isolierte CPU-intensiven Hotspots erst mit Benchmarkbeleg; minimale versionierte FFI, keine ungesicherten Speicherübergaben. **C++/OpenCV:** bewährte Tracking-Funktionalität behalten, nicht ohne Parity-Test neu schreiben. **FFmpeg:** Medienzugriff, echte PTS/VFR und Decoder; Licensing-/Build-Audit. **JS/CSS:** vorhandenes Wails-Frontend weiterentwickeln; kein großes Framework ohne messbare Notwendigkeit. **KI:** optionale lokale Adapter, keine kritische Abhängigkeit für Classical Generate.

**Eigenentwicklung mit System:** Methodenwahl, Quality-Scoring, RetryPlaner, reversibler Editor, Gerätecompiler, Profile, Lernfeedback und reproduzierbare Benchmarks als eigenes geistiges Eigentum. Fremdbibliotheken für Codec/CV/Matrizen/Hardwareprotokolle nur mit klaren Lizenz- und Sicherheits-Audits.

## 3. Bestehende Komponenten übernehmen

| Basis | Herkunft in Free | Next Level Aufgabe |
|---|---|---|
| Go/Wails App, Play/Create/Edit | `cmd/gui-wails` | Produkt- und GUI-Grundstruktur behalten |
| CSRT/Optical Flow, ROI, Signalfilter | `generator/`, `trackcv` | Referenz-Backend, Goldens, danach optionale native Ports |
| Quality Doctor | `generator/quality_doctor.py` | Per-Szene unabhängige Bewertung und Vergleich |
| SAM + Funscript + .samn | `sam/`, `funscript/`, Formate | Zeitbasis, Herkunft, Multi-Axis, rückwärtskompatible Migration |
| ONNX/AI ROI, VLM, AI-Draft | bestehende KI-Module | Adapter, optionales Lernen und geprüfte Kurvenentwürfe |
| Geräte/Intiface/Neo2 | `device/` | echtes Capability-Modell + sicherer Ausgabekompilierer |
| Frontend Flow/Training-Ringe/Icons | Merge #509 | tatsächliche GUI, nicht Mockups |
| vorhandene Regressionstests | Go/Python/Frontend | Parität vor jeder größeren Portierung |

## 4. Meilensteine (in Gates, nicht in Kalenderwochen)

| Gate | Baustein | Verbindliche Abnahme |
|---|---|---|
| N0 | Code & vollständige Git-Historie aus privatem Free sichern/übernehmen | Eigentümer bestätigt Quellcodefreigabe für öffentliches Repo; Lizenzen/Secrets/History geprüft; Basis baut |
| N1 | Freeze und Baseline-Messsystem | echte Golden-Clips lokal, synthetische Fixtures, Referenzskripte, Laufzeit/CPU/RAM/Qualitätsreport reproduzierbar |
| N2 | Native Contracts + Timebase | `TrackResult` rohe px/PTS/Validity; `Candidate`; `CurvePoint`; `.samn` roundtrip; VFR/Seek-Tests |
| N3 | Stabile Engine Classical 1.0 | Single-Clip ohne KI reproduzierbar mindestens Free-Parität |
| N4 | Automatische Szenenwahl | Scan, ROI/Scene-Kandidaten, Tracking-Fallback, Reason Codes, selektive Retries; keine unbelegten „Confidence“-Werte |
| N5 | Unabhängige Qualitätsprüfung | Kandidat nur mit Verifier übernehmen, Szene markieren statt schlechtes Ergebnis verschleiern |
| N6 | Device Compiler | Mock 1/2/6 Achsen und Vibration/Suction, Caps, Stop, Disconnect; echte Hardware vom Owner abgenommen |
| N7 | Editor Foundations | Mehrspur-Projektmodell, Master/Companion-Export, Undo/Redo, transaktionale Operationen, VFR-Zeitachse |
| N8 | Full Multi-Axis Editor | Auswahlen/Transform/Snapping/Bezier oder geprüfte Interpolationen/A-B/Audio/History/Scene-Inspector; 100k Aktionen |
| N9 | Optional Hybrid AI | LM Studio/ONNX nur opt-in, Mock-Ausfall, Bildrecht/Privacy, Draft/Repair nur via Verifier, Fallback jederzeit |
| N10 | Local Learning | bestätigte Korrekturen, lokale Versionen und Undo, Shadow Mode, Daten löschen/exportieren; keine automatische Sammlung |
| N11 | Professional Batch | persistenter Jobgraph, Resume/Retry/Cancel, Limits CPU/GPU/RAM, Reports, sichere Exportpfade |
| N12 | Performance/Polish/Commercial | CPU/GPU Bake-offs, echte Nutzer- und Hardwaretests, GUI/Accessibility/Installer/Terms/Payment/Supporter |
| N13 | International Release | nur bestätigte Features, Changelog, Download/Checksums, User Handbook, Support & Rückerstattungspfade |

Jedes Gate hat: Implementation, automatische Tests, Golden-Vergleich, dokumentierten Benchmark, Windows-11-Smoke, Review mit Claude und Freigabe durch den Eigentümer. Reihenfolge begründet; kleinere unabhängige Design-/Test-Aufgaben können parallel laufen.

## 5. Qualitäts- und Performancesystem

**Messungen:** Timing-Fehler (PTS gegen Video und ggf. Referenz), ROI-Verlust, Sprünge/Jitter, Signal- und Frequenzvergleich in kurzen Fenstern, Track-Kontinuität, Amplitude vor Normalisierung, Szene-Nahtstellen, Export-Roundtrip und Device-Cap-Verletzungen. Nur mit Ground Truth absolute Genauigkeit behaupten. Bei Alternativen muss auf Baseline zurückgefallen werden, wenn die neue Kurve keine nachweisbare Verbesserung liefert.

**Performance:** gleiche Clip/Hardware/Config, Warm-ups und wiederholte Läufe, Median + p95, CPU/GPU/RAM-Spitze, Frames/s, Laufzeit. **Keine Rust-Portierung allein wegen Rust.** Regression gegen Golden bedeutet Go/FFmpeg/C++ behalten oder zurückrollen.

**Automatischer Prüflauf Windows 11:** Go/Python-Altbestand/Frontend/Rust (sobald vorhanden), Unit-Tests, deterministische synthetische Videos, VFR, Cancel, Crash, Seek, Import/Export, Device Mock. Danach manuelles Native-Wails-QA 1366×768 und 1920×1080 mit echter Wiedergabe und Mock/Hardware. Keine privaten Video-Assets im öffentlichen Repo.

**Mac LM Studio:** lediglich optionaler per LAN freigegebener OpenAI-kompatibler Modell-Endpunkt. Der Mac macht nicht automatisch „Training“ durch Inferenz; echte Lernzyklen laufen kontrolliert über Label-/Feature-Datensätze, Trainingsjobs, getrennten Validierungs-Korpus, Modellversion und Rollback. Kein heimlicher Upload von Videoframes, auch nicht über LAN.

## 6. Geräte von Anfang an universell denken

Neo2 als getesteter erster Adapter, aber der Kern darf **niemals** zwei feste Kanäle voraussetzen. Pluggable Capabilities: L0 Stroke; L1/L2; R0–R2; Vibrate (mehrere); Suction; Rotation, nur wenn wirklich verfügbar. Weitere Zielgeräte: The Handy, Lovense, LELO F1S, OSR2/SR6, Intiface-kompatibel. Manche Geräte brauchen eigene Protokoll-/Latenzadapter. Masterkurve wird nicht in erfundene Achsen kopiert. Mock-Gerät mit frei konfigurierbarer Capability-Matrix ist erster Test.

## 7. GUI- und Editorstrategie

**Look:** markanter Flow-Stil mit Neon-Cyan/Orange als klare Signale, dunkles professionelles UI, sichtbare Video-Arbeitsfläche. Keine undurchsichtige Kontrollwüste. Drei Komplexitätsstufen Auto/Guided/Expert; alle Funktionen echt verdrahtet, keyboard- und reduced-motion-fähig. Für Edit: Video, präzise Timeline, unabhängige Achsen/Lanes, Ghost/Compare, Inspector, Chapters, Waveform, Scene Map, Undo-History; nur echte Messdaten/Device-Outputs anzeigen. Reale 1366×768/1920×1080 Windows-Screenshots als QA, keine Renderziele als released UI ausgeben.

## 8. Code-Ownership, CI, externes Wissen

Claude: Go/Rust/native/core/wiring, Tests, Windows Build & QA. ChatGPT: Frontend visuals/CSS, Produktdokumente, Benchmark-Konzept und selbstständige Test-/Designmodule in eigenen Branches. Quellmodule nicht auf fremden Agent-Branches überschreiben.

Externe Bibliotheken nur nach **Lizenz, Dependency-Kosten, Paketgröße, Sicherheitsrisiko, Datenprivacy, Wartung und gemessenem Mehrwert**. OpenFunscripter GPL nicht blind in proprietäre Software kopieren. MultiFunPlayer MIT nur mit Attribution/Dependency-Audit verwenden; primär interoperable Formate und nachgestellte eigene Implementierungen. Eigene synthetische Tests und rechtlich freigegebene lokale Testvideos.

## 9. Wichtige Release- und Geschäftsgrenzen

- Community Free erhält später klaren Kernumfang, reduzierte KI, keine dauerhafte Batch-Verarbeitung, weiterhin Donate. Ausgewählte **neue** Funktionen können in Free explizit **7 × 24 Stunden** als Preview freigeschaltet werden; keine Scripts/Dateien laufen ab. Eigene Entwicklung dafür im Free-Repo.
- Interne **Suite** ist Entwicklung/Tests und nicht automatisch dasselbe wie das kommerzielle Studio.
- **NEUE EIGENTÜMERENTSCHEIDUNG (12.10.2026): Next Level ist SamNPlayer 2.0, kostenlos als proprietäre Freeware mit freiwilligen Spenden.** Frühere Pläne für kostenpflichtiges Studio, Kauf-/Lizenzschlüssel, Bezahl-Gates und garantierte €40-Zugangsrechte sind aufgehoben. Spenden gewähren keinen exklusiven Zugang. Vor Veröffentlichung EULA/Website und Kostenhinweise an die tatsächlich kostenlose Version anpassen.
- Keine sofortigen Produkt-/KI-/Geschwindigkeitsversprechen, bevor gemessen und getestet.
- **Dieses Repository ist aktuell öffentlich.** Alle Planunterlagen sind zur Veröffentlichung vorgesehen; keine geheimen Schlüssel, privaten Nutzervideos, sensitiven Lernsets oder nicht freigegebene private Git-Historie kopieren.

## 10. Konkrete ersten 10 Aufgaben

1. Eigentümer bestätigt explizit, ob die **komplette bisher private Quellcodehistorie öffentlich** werden darf (nicht nur diese Roadmap).
2. Audit Free-Repo Bestand / Secret-Scan / Third-party Notices; erst dann sauberer Git-Import.
3. Windows-11-Maschinenprofil und reproduzierbare Setup-Checks ohne private Schlüssel dokumentieren.
4. Go-Schnittstelle für PTS/TrackResult/QualityReport als pure Typen mit Tests definieren.
5. Golden-Clip-Manifeste/Synthesegenerator erstellen, keine echten sensiblen Videos veröffentlichen.
6. Bestehenden Free-Generator per Adapter als unveränderte Referenz im Next Level starten.
7. Erste reproduzierbare Batch-Einzelclip-Benchmarkstrecke automatisieren.
8. SAM/.samn Multi-Axis-Kompatibilität gegen Free mit Fixtures testen.
9. Eine frei konfigurierte Device-Mock-Matrix mit Safety Governor testen.
10. UI-Prototyp aus funktionalen Kern-Events für Auto/Guided/Expert aufbauen; keine funktionslosen Knöpfe.

## Neueste Produktentscheidung — ersetzt frühere Studio-Monetarisierung

- **SamNPlayer 1.x (Community/Free):** stabile, gute und einfache Freeware; Spendenbutton bleibt. Nicht künstlich schlechter machen.
- **SamNPlayer Next Level 2.0:** neue Hauptgeneration aus vorhandenem SamNPlayer-Code mit gleicher vertrauter Flow-GUI, modular erweitert um native Go/Rust-Optimierung, autonome klassische Analyse und optionale geprüfte KI, vollwertigen Multi-Axis-Editor und erweiterte Batch-Funktionen. Kostenlos, spendenfinanziert, proprietär.
- Keine Studio-Kaufabwicklung, keine €40-Schwelle und keine Bezahllizenz mehr als Roadmap-Blocker. AI und neue Features werden nicht kostenpflichtig gesperrt. Diese neuere Entscheidung hat Vorrang vor abweichenden älteren Abschnitten in anderen Entwürfen.
- 7-Tage-Funktionsvorschauen waren für Free vorgeschlagen; vor Bau prüfen, ob und wozu zeitliches Abschalten noch sinnvoll ist, wenn Next Level ebenfalls kostenlos wird. Keinen künstlichen Trialzwang einbauen.
- Rechtliche Hinweise für freiwillige Spenden, Drittanbieter-Lizenzen und die proprietäre Lizenz bleiben erforderlich.
