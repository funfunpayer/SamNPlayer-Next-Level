> **Archived input** — copied unchanged from `funfunpayer/SamNPlayer` PR #510 (head `121acd7`, 12 Oct 2026, not merged).
> Superseded where it disagrees with [`docs/MASTER_PLAN.md`](../MASTER_PLAN.md). Do not edit; change the master plan instead.

# SamNPlayer — Free → Studio: verbindlicher Produkt-, Technik- und Launchplan

**Datum:** 12.10.2026 · **Status:** Konzept/Arbeitsvorlage für Owner und Claude, noch kein Live-Preis und keine EULA-Änderung.  
**Produkt:** EIN bestehendes SamNPlayer (kein zweites Programm, kein „2.0“-Neustart).  
**Abfolge (Owner):** Free optimieren → technisch/optisch aufräumen → dokumentieren → Website aktualisieren → Free veröffentlichen → Resonanz/Qualität messen → Studio entwickeln und verkaufen; Free bleibt erhalten.  
**Ziel:** „Video + Ziel + optionales Gerät → weitgehend automatisch hochwertiges, überprüftes Master-Funscript, das zum Video und zur konkreten Gerätefähigkeit passt; Nutzer kann eingreifen.“

## 1. Beschlossen und nicht verhandelbar

- **Free bleibt** als voll nutzbarer Player/Create/Edit/Training/Tools; keine nachträgliche Entfernung bisher ausgelieferter Free-Funktionen ohne gesonderte Owner-Entscheidung. Sicherheits- und Not-Stopp-Funktionen niemals paywallen.
- **Studio wird Premium** für die fortgeschrittene **adaptive automatische Analyse/Methodenwahl, selektiven Retry, AI-gestütztes Repair/optional AI-Draft und Batch-Verarbeitung**, nicht eine zweite App; gemeinsame Codebasis und Formate.
- **Klassische Analyse funktioniert ohne KI.** KI ist optional und nutzt Daten der vorhandenen Verfahren; sie kann auch Kurvenentwürfe schreiben, wenn Quality Doctor + unabhängige Video-/Zeitbasis-/Geräteprüfung bestanden sind.
- **Ein Master-Funscript**, kompatibler Standardexport; interne SAM-Motion-/Confidence-Metadaten und .samn-Projektformat erweitern, nicht ablösen. Device Adapter projiziert nur tatsächlich unterstützte Achsen.
- **Go als Orchestrierung und Produktkern; Rust/C++ für gemessene native Hotspots.** Python möglichst aus Endnutzer-Runtime, aber nicht blind OpenCV/NumPy/SciPy umschreiben; Golden-Qualität vor Performance-Behauptung.
- **Privacy-first, lokal:** keine Telemetrie, keine heimlichen Uploads, Modelle nur ausdrücklich installiert/aktiviert; Lernsammlung opt-in mit Löschen/Export.
- Spenden finanzieren Weiterentwicklung und neue Free-Funktionen, **nicht** automatisch Kaufverträge oder Features, bis separates rechtliches Reward-/Studio-Angebot eingerichtet ist.

## 2. Tatsächliche Ausgangsbasis / Berührungspunkte im Repo

| Bereich | Heute | Nächste Handlung |
|---|---|---|
| UI | Wails Go/JS, Player Play/Create/Edit, Training, Tools | #509 C1–C4 integrieren, #489 visuellen Zielabgleich durchführen, native 1366×768/1920×1080 QA |
| Generator | `generator/generate_funscript.py` CSRT/OpenCV + Kameraausgleich, klassische Filter/Peaks; `backends.py` Backendvertrag | Goldens verifizieren; Timebase-/Qualitätscontract erfassen; native Migration modular |
| Qualität | `generator/quality_doctor.py` (Lücken, Spikes, Timing, Rhythmus) | Qualitätsreport pro Szene, unabhängig vom Kandidaten; kein „100 % perfekt“ ohne Ground Truth |
| KI | `ai_roi.py` ONNX, `vlm_probe.py` lokale VLM-Probe, Go profilemodel | optional ROI, Bewertung, Refiner/Draft; fallback classical |
| Dateien | `.funscript` kompatibel, interne SAM + `.samn` | kanonische Zeitachse, alle Entscheidungen/Undo/Provenance versionieren |
| Devices | Neo 2 + Intiface Basis, Capabilities D1; D2–D5 geplant | Adapter + Sicherheitsgrenzen + Mocktests vor Freigabe |
| Recht | `LICENSE`, `legal/EULA.md` vom 11.10.: proprietäre Freeware, Spenden ohne Gegenleistung | Free unverändert lassen; eigene Studio-Angebots-/EULA-/Steuerprüfung vor Verkauf |
| Website | separater öffentlicher `SamNPlayer-site` mit index.html, script.js, style.css, SETUP.md | Launchdaten, echte Screenshots, verifizierte Downloadlinks, Datenschutz/Terms/Support |

## 3. Produktschnitt Free und Studio (vorläufige konkrete Featurematrix)

| Fähigkeit | Free (geplant dauerhaft) | Studio (geplant) |
|---|---|---|
| Play/Stop/Pause, Medien-/Scriptbibliothek, Standard-Funscript-Import/Export | Ja | Ja |
| Basis-Create, manuelle ROI, bewährtes CSRT, einfache automatische Erkennung, sichere Script-Validierung | Ja | Ja |
| Edit, Scene Map, Audio-/BPM-Hilfen, Undo, elementare Filter, Training | Ja, soweit bereits vorhanden/ausgereift | Ja |
| Device Support inkl. sichere Caps, Emergency Stop, synchrones Standardmaster-Script | Ja | Ja |
| **Adaptive Multi-Backend-Analyse** pro Szene mit Kandidatenvergleich und selektivem Retry | Nein (Baseline bleibt) | Ja |
| **Batch/Queue** mehrere Videos mit gespeicherten Jobs, Retry, Exportprofilen, Fortschritt, Pause/Resume | einfacher bestehender Queue-Pfad bleibt; keine rückwirkende Streichung | Profi-Batch mit Presets, Fehlerfortsetzung, Reports |
| **Advanced AI Assist** inkl. optionaler KI-Kurvenreparatur/AI-Draft + Gegenprüfung | vorhandene lokale KI-Hilfen bleiben erhalten | neue Studio-Automatik, erklärbarer Compare/Apply |
| Qualitätsreport pro Szene, Multi-Kandidaten-Vergleich, Regeneration nur problematischer Bereiche | Grundlegende Validierung | Studio-Workflow |
| Studio-Projektprofile, Export für mehrere Geräte und fortgeschrittene Preset-Verwaltung | Standardprofil/Standardexport | Mehrfach-Export/Batch |
| Sicherheit, Privatsphäre, Verarbeitungsrechte an eigenen Dateien | niemals künstlich eingeschränkt | identisch |

**Release-Gate für Studio:** Premium darf nicht „umschaltete Zukunft“ oder Placebo sein. Mindestens ein messbar funktionierender End-to-End-Profi-Batch **und** eine adaptive Pipeline mit Golden-Clip-Parität müssen vorhanden sein, bevor Zahlung aktiviert wird. Keine kaputten Knöpfe als Upsell.

## 4. Studio-Engine innerhalb des existierenden Programms: konkret

### Nutzerpfad

1. **Video(s) wählen**, optional Zielklasse/ROI/Gerät/Profil. Default: Universal + Auto.
2. **Fast Scan (ohne KI):** Stream/PTS/VFR, harte Szenenwechsel, Kamera-/Bewegungsenergie, Audio-BPM nur unterstützend. ROI-Kandidaten und Unsicherheiten markieren.
3. **Classical Baseline:** bestes bewährtes Trackingverfahren (CSRT mit bestehenden Schutzmaßnahmen), per Szene Track und Gültigkeitsmaske erstellen.
4. **Adaptive Entscheidung:** pro Szene nur bei konkreten Warnsignalen (Verlust, Drift, Occlusion, Aliasing, schlechter Bewegungsbereich) weitere **bereits vorhandene Backends** laufen lassen. Scores beruhen auf realen Metriken: Zieltreue, Lost Frames, Rohpixel-Amplitude, Kameraausgleich, Zeitstempel, Geschwindigkeits-/Sprunggrenzen, Audio-Konsistenz als Hilfsindikator. Unkalibrierte „Confidence“ nie als Wahrheit ausgeben.
5. **Optional KI:** ROI-/Szenenvorschlag, lokaler Curve-Refiner oder Script-Draft **auf Basis des Tracking- und Audio-Befundes**, nicht freie LLM-Positionshalluzination. Modelle über vorhandene ONNX/lokale VLM-Adapter; z.B. Ministral-Vision nur nach Lizenz-/Hardware-/Cliptest.
6. **Verifier:** harte Syntax-/Zeitbasis-/Bereichsvalidierung, Quality Doctor, Tracking-/Videoevidenz, Glättungs-/Kontakt-/Gerätefähigkeit. Nur nachweislich besserer Kandidat ersetzt Baseline, sonst Baseline + Review-Hinweis.
7. **Master-Script speichern:** eine Standard-Positionskurve (funscript) + `.samn` für Quelle, Segmententscheidungen, SAM-Metadaten, optional echte weitere Kurven. Pro Gerät nur zulässige Kanäle projizieren.
8. **Benutzer kann eingreifen:** einzelne Szene ändern/markieren, Kandidaten vergleichen, Alternative regenerieren, Undo; bestätigte Korrekturen nur nach Opt-in für lokale Verbesserung verwenden.

### Native Migrations- und Modul-Contracts (Arbeitshypothese)

```text
Wails UI -> Go Job API
  -> VideoProbe/FrameCache (FFmpeg, PTS-aware)
  -> SceneScanner (classical)
  -> BackendRegistry [Legacy Python / native Go / native Rust/C++]
  -> CandidateScorer -> SelectiveRetry -> CurveComposer
  -> optional LocalAIAdapter -> IndependentVerifier
  -> SAM/.samn + universal funscript -> Device Adapter & safety governor
```

- **TrackResult:** `time_ms[]`, `position_px[]`, `valid[]`, `frame_size`, `backend_id/version`, `scene_id`, `stats`; Position **vor** finaler Normalisierung in Bildpixeln, da Qualitätsbewertung Rohamplitude benötigt.
- **DecisionResult:** selected backend, alternates, failed hard-gates, quality metrics, reason codes, review-required; nachvollziehbar und reproduzierbar.
- **CurveSegment:** Zeiten, Position 0..100, Gültigkeit, Provenance, optional confidence; keine ausgedachten Zwischenwerte bei echten Lücken.
- **JobState:** queued/scanning/tracking/verifying/retrying/review/complete/failed/cancelled; Progress-Werte aus ausgeführten Schritten, Cancel/Timeout/Resume und Cacheinvalidation bei geänderten Inputs.
- **DeviceOutput:** Masterpositionskurve plus vorhandene Achsen, transformiert nur durch Adapter und limitiert durch Safety Governor; keine direkte KI→Toy-Kommunikation.

**Performance-Ziele als Zielwerte, NICHT Ist-Behauptungen:** Golden-Gates: bei identischem Video keine Qualitätsverschlechterung; für einen akzeptierten Native-Port zunächst ≥20 % geringere Laufzeit *oder* ≥20 % weniger Peak-RAM bei gleicher Qualität als Mindest-Hypothese, echte Schwellen nach Basisbenchmark festsetzen. Szenen-/Hardwarematrix: 1080p/30, 1080p/60, VFR, Kamerawechsel, 10 min und 60 min, schwache und starke GPUs. Messen: wall-clock, frames/s, CPU/GPU, RAM peak, Tracking-Lost-Frames, windowed lag/korrelation, Position/Timing-Fehler (bei Referenz), Device-Cap-Limitverletzungen. Separat Ausreißerreport. Rust nur auf nachgewiesenen Hotspots.

### Lernsystem und Datenschutz

- **Korrekturereignis** nur bei bestätigter Nutzerentscheidung: SceneID, Feature-Fingerprint, ursprüngliche und bestätigte Methode/Curve, Modell-/Backendversion, Veränderung; ohne Videoframes in Logs.
- Sammlung/Training/Export opt-in getrennt, standardmäßig aus; lokale Speicherbegrenzung, Export/Löschung, Model-Rollback.
- Neues Selector-Modell zunächst *shadow mode*: Empfehlung ohne automatische Übernahme, Golden-Auswertung vor Defaultänderung.
- Kein pauschaler Upload von Korrekturen für ein globales Modell ohne gesonderte Einwilligung/Prüfung der Rechte und sensiblen Daten.

## 5. Profi-Batch als erste klar verkäufliche Studio-Funktion

**Batch-Projekt:** persistente Jobliste mit mehreren lokalen Videos, Zielpreset, optional Geräteprofil, separatem Ausgabeordner und Quality-Policy (Auto/Review).  
**Queue:** mindestens queued/running/paused/failed/complete/cancelled; pro Video pro Szene Einzelstatus; nach Crash/Neustart fortsetzbar; idempotenter Export/kein Überschreiben ohne Einstellung.  
**Ressourcen:** einstellbare Anzahl Analyse-Jobs, GPU-exklusiver Slot, CPU/RAM-Limits; niedrige Priorität im Hintergrund optional, keine parallele Decoderflut.  
**Fehlerpolitik:** ein Clipfehler stoppt nicht zwingend den Batch; Error-Report je Clip, Retry nur fehlerhaften Clip/Szenenteil.  
**Ergebnis:** `.samn`, `.funscript`, optional begleitende Achsen, JSON/CSV Quality-Report, Versions-/Provenance-Daten.  
**Testabnahme:** 20 repräsentative Clips (inkl. VFR, ohne Audio, defekt, kurze Szene, Unicode-Dateiname, schreibgeschützter Zielordner), Cancel/Resume/Restart, kein Datenverlust; kein unbeabsichtigtes Überschreiben und kein falscher „fertig“-Status.

## 6. Release-Zug: zuerst Free professionell abschließen

### F0 — PR- und Konflikt-Aufräumen (vor Release)

- Claude prüft **#509 Integration** gegen C1 **#503**, C2 **#505**, C3 **#507**, C4 **#508**. Single-source-of-truth für alle Dateien: keine doppelten Komponenten oder kollidierenden Importe. Auf Hauptbranch erst nach Review + grünem CI + realem Wails-Smoke.
- **#489** CSS im Zusammenhang mit #509 testen und visuellen Vergleich zu fünf Zielbildern auf 1366×768 und 1920×1080 durchführen. Nicht nur Mockups vergleichen: Play/Pause/Stop/Focus/Reduced Motion, Create/Edit, Training, Device, Szene, Drag/drop, responsive.
- **#483, #485, #486, #479, #500** nacheinander gegen main / #489 / #501 bewerten, Inhalt erhalten oder ausdrücklich als überholt schließen mit Hinweis auf Ersatz-PR. Aktive Arbeiten nicht blind löschen. Alte Dokumentations-PRs #468/#471 gesondert einordnen.
- Merge nur nach PR/CI/Review; alle getrennten ChatGPT-Branches erst nach nachweislich integrierten Änderungen schließen/löschen; GitHub History behalten.

### F1 — Repo-Reinigung (kleine isolierte PRs)

1. **Bestandsinventar**: Module, Wails-Bindings, Python-Laufzeit, Modell-Abhängigkeiten, tote Imports, Duplikate, Legacy-Docs; `docs/GUI_AUDIT_2026-10.md` gegen aktuelle Version aktualisieren.
2. **Dokumenten-Single-Source-of-Truth**: `docs/PRODUCTION_ROADMAP.md` (zeitliche Releaseplanung), dieses Konzept (Free/Studio), `docs/AI_ADAPTER.md` / `docs/AI_SCRIPT_WRITER.md` (hybride Engine), `docs/DEVICE_SUPPORT.md`; alte „5 top tabs“/„3 axes hard limit“/alte Versionsangaben nicht mehr prominent. `docs/SAM_ARCHITECTURE.md` als interner Modellvertrag bestehen lassen.
3. **Tests**: automatische Go/Python/Frontend-Tests, neue Browser-Smokes für C1–C4 ausdrücklich in CI aufnehmen; native Windows Smoke; keine externen privaten Video-Assets in Screenshots, README oder öffentliche Testartefakte.
4. **Secrets/Assets**: kein Videomaterial, keine personenbezogenen Korrekturdaten, keine privaten Schlüssel/Modelle in Releases; Copyright/third-party-notices, SHA256 Artefakte, überprüfbare Versionsnummern.
5. **Repository-Hygiene**: verwaiste Branches und überholte PRs erst nach Referenzprüfung schließen; alte Dateien nicht löschen, wenn Release/CI/Tests sie benötigen.

### F2 — Free-Releaseabnahme („go/no-go“)

**Freigabekriterien:** sauberer main, alle erforderlichen CI-Suites grün, Windows Wails manuell auf 1366×768/1920×1080 geprüft, durchgängiger Generate→Save→Play→Device-Mock-Pfad, Script-Import/Export, Cancel/Fallback/No Audio/No Model, Update-/Edition-Sicherheit, EULA und Not-Stopp geprüft, reale Release-Zip/Signatur/Checksums geprüft, Dokumentation und Zielbilder stimmen mit dem Build überein. **Erst dann** Tag/Release veröffentlichen; keine unbestätigten Versions- oder Downloadbehauptungen.

### F3 — Öffentliche Website `SamNPlayer-site`

- Header/CTA: **„SamNPlayer Free“** / „Download Free“; eindeutig **proprietäre Freeware**, **nicht Open Source**.
- Erklären: „Generate · Play · Train“, unterstützte Geräte *nach tatsächlich veröffentlichtem Build*, Funscript/.samn, lokale Verarbeitung, optionaler Modelladapter, keine ungeprüften Wunder-Aussagen.
- Bereiche: Features mit realen Screenshots, Download + bestätigte Version / Datum / OS-Voraussetzungen, Installationshinweise, FAQ, Grenzen des Beta-Status, Changelog, Bugs/Feedback/Tester-Forum, Datenschutz, Impressum, EULA/Third-party notices, Donate.
- Grafikdateien nur aus **tatsächlich abgenommenem Build**; nicht Visual Targets als fertige Screenshots ausgeben.
- Einen **„Studio – in Entwicklung“**-Abschnitt erst mit ehrlichem Funktionsumfang; kein Vorverkauf ohne lieferbaren, definierten Leistungsumfang/Terms.
- Footer und Links prüfen: Github public site, Release Assets, Support, Donations, Rechte an Visuals.

### F4 — Free veröffentlichen und Resonanz erfassen

- **Tag 0**: Website/Download, Changelog, Handbuch, 2–3 überprüfte Forenbeiträge; Beta-Tester gezielt um Rückmeldungen zu Qualität/Hardware/Bedienung bitten.
- **Woche 1–2**: Bugtriage, reproduzierbare Clips/Logs nur freiwillig, privacy-sicher; Releasefixes vor neuen Großfeatures.
- **Woche 3–4**: Featurewünsche/Usability bündeln; pro Engineproblem Szenenmetrik, Tracking/Quality, Modell und Gerät erfassen. Keine automatische Telemetrie.
- **Studio-Go-Gate:** Free stabil genug, reproduzierbare Engine-Baselines vorliegend, Batch-Mehrwert nachweisbar, Supportkapazität und Payment/Legal durchdacht. Kein erfundener Download-/Nutzer-KPI; freiwilliges Testerfeedback und Supportlast reichen als erste Signale.

## 7. Geschäftsmodell: Free + Studio + Finanzierung der Free-Entwicklung

### Editionen (Designentscheidung zur Abnahme durch Owner)

| Edition | Preisidee | Kern |
|---|---:|---|
| **Free** | 0 € | heutige Funktionen bleiben; Community-Bugfixes, grundlegende Create/Play/Edit/Training/Device-Sicherheit |
| **Studio** | Vorschlag **49 € einmalig** für erste kommerzielle Version (vorläufig!) | native adaptive Generierung + fortgeschrittener Batch, KI-Assist/Repair soweit technisch bewährt, Reporting/Presets |
| **Studio Upgrade** | später optional kostenpflichtiges Major-Upgrade | Kein Abozwang; kritische Sicherheitsfixes bestehender Kaufversion bereitstellen |
| **OEM/Business** | individuelle Vereinbarung | kommerzielle API/Integrationen/Support – erst bei nachgewiesener Nachfrage |

**Preis ≠ bestätigt:** 49 € ist eine absichtliche **Hypothese im Umfeld der 40-€-Unterstützeridee**, nicht aus Marktforschung abgeleitet. Frühzeitige Nachfrage mit unverbindlicher Warteliste validieren. Laufende Modell-/Cloud-Abos sind nicht geplant.

### Die 40-€-Idee sauber und rechtlich belastbar lösen

Der Owner wünscht: „Wer mehr als 40 € spendet, erhält automatisch Studio.“

**Problem:** Wird Studio **verbindlich gegen eine Zahlung** versprochen, kann diese wirtschaftlich und steuerlich **ein Kaufpreis/entgeltliche Leistung** sein, selbst wenn der Button „Spende“ heißt. Aktuelle `legal/EULA.md` §6 sagt ausdrücklich, Spenden kaufen **keine Lizenz/Extra-Funktionen**. Das darf vor Umsetzung nicht widersprüchlich bleiben.

**Empfohlene zwei getrennte Zahlungswege:**

1. **Freiwillige Spende („Support Free Development“)**: beliebiger Betrag, keine zugesagte Gegenleistung, kein Studio-Anspruch; kommunizierter Zweck: Entwicklung neuer Free-Funktionen, Tests, Infrastruktur. Keine steuerliche Spendenbescheinigung vortäuschen.
2. **Studio Supporter-Pass („Supporter Edition“) als echter Kauf**: **40 € oder mehr als definierter Preis** (zum Beispiel *40 € einmalig*), ab tatsächlicher Studio-Verfügbarkeit ein Studio-Recht; eigener Checkout, dokumentierter Lieferumfang, VAT-/Verbraucherinfos, Bestellbestätigung, Rechnung, Refund/Support, technisch eindeutiger Entitlement-Nachweis. Größerer freiwilliger Zusatzbeitrag **getrennt** ausweisen, wenn Payment/Steuerkonzept es erlaubt.

**Alternative:** freiwilligen Unterstützern später manuell ein Geschenk ohne rechtliche Verpflichtung anbieten; damit aber **nicht** vorher als garantierte Regel werben. Vor Free-Launch weiter nur Spenden ohne Gegenleistung.

### Technische Lizenzierung, ohne Privacy-Verlust

- Keine bereits entfernte Key-Infrastruktur still reaktivieren. Zuerst Modellentscheidung, Threat Model, Offline-Ziel, verlorener Kaufbeleg, Updates, Gerätewechsel, Rückerstattung und Support entwerfen.
- EINE Binärdatei oder zwei Editionen? Empfehlung: gemeinsame Codebasis; ob gemeinsamer Build mit Feature-Entitlements oder separate Free/Studio-Artefakte entscheiden anhand bereits bestehender Release-/Updater-Logik (#506 Edition Match). Keine Security-by-CSS: Features müssen backendseitig korrekt gated werden; Safety/Free nie sperren.
- Offline-Erfahrung: lokal prüfbarer signierter Kaufbeleg/Entitlement (Public-Key-Verifikation), Ablauf nur falls explizit Teil der Verkaufsbedingungen; keine permanente Serverpflicht.
- Payment-Plattform muss das konkrete **Adult-nahe Softwareprodukt** akzeptieren. Anbieter-/AGB-Prüfung VOR Festlegung und Marketing; keine ungeprüfte Händlerempfehlung.
- Kauf kann EU-Verbraucherrechte, Umsatzsteuer (inkl. digitaler Leistungen/OSS je Vertrag), Rechnungs- und Widerrufsfragen auslösen; **fachkundige Prüfung** der tatsächlichen Unternehmens-/Steuersituation und Rechtstexte vor Checkout.
- Historische MIT-Veröffentlichungen begrenzen nicht die Monetarisierung eigener künftiger proprietärer Releases, aber zuvor eingeräumte Rechte bleiben bestehen. Third-party-Komponentenlizenzen für alle Editionen beachten.

### Datenschutz und Vertrauen

- Videos/Funscripts bleiben lokal, keine automatische Modellinstallation; Logging nur technische Metriken ohne intimen Content; opt-in Feedback und Löschfunktion.
- Keine verdeckte Telemetrie „für Studio-Conversion“. Rückmeldungen per Form/Issue/Forum freiwillig.
- Keine falsche Aussage „spenden steuerlich absetzbar“ oder „Einnahmen garantiert vollständig nur für Entwicklung“, wenn Kosten/Steuern abgehen.

## 8. Operative Roadmap und Tickets (in abhängiger Reihenfolge)

| ID | Prio | Owner | Abhängigkeit | Deliverable / Abnahme |
|---|---|---|---|---|
| FREE-01 | P0 | Claude | #509 | C1–C4 korrekt integriert, ohne doppelte Dateien, Build/CI und native Tests |
| FREE-02 | P0 | ChatGPT CSS / Claude Core | FREE-01, #489 | fünf reale GUI-Ansichten 1366/1920 gegen Zielbilder geprüft, sichtbare States/focus/reduced-motion |
| FREE-03 | P0 | Claude | FREE-01/02 | alte PRs/Branches gezielt reconciliert; Release main ohne ungekürzte Rückstände |
| FREE-04 | P0 | Claude | FREE-03 | Regressionstests Generate->Save->Play, Devices Mock, Import/Export, Error/Cancel, dokumentierte Ergebnisse |
| FREE-05 | P0 | ChatGPT Docs / Claude technische Prüfung | FREE-04 | README, Changelog, Handbuch, Architektur-/Lizenz-/Releasehinweise wahrheitsgemäß |
| FREE-06 | P0 | ChatGPT Website / Claude Release | FREE-05 | SamNPlayer-site PR mit überprüften Screenshots, Download, CTA, FAQ und Donation |
| FREE-07 | P0 | Claude / Owner | FREE-06 | verifizierter Free Tag/Release und Smoke; Website zeigt exakt diese Version |
| FREE-08 | P1 | Owner/Community | FREE-07 | 2–4 Wochen Opt-in Feedbacktriage und golden clip failure corpus |
| ENG-01 | P1 | Claude | FREE-07/08 | Runtime Python dependency inventory + Benchmark Harness + Golden report |
| ENG-02 | P1 | Claude | ENG-01 | Go backend/track contract, Classical baseline + fast scan, **keine Ausgabeänderung** |
| ENG-03 | P1 | Claude | ENG-02 | per-scene candidate compare + independent verifier + manual review |
| ENG-04 | P1 | Claude | ENG-03 | selektiver Retry, measured performance, keine Qualitätsregression |
| ENG-05 | P1 | Claude | ENG-04 | opt-in AI ROI/Refiner/Draft mit Gate und classical fallback |
| ENG-06 | P1 | Claude | ENG-01 | einzelne Go/Rust-Ports bei golden parity + gemessenem Vorteil |
| STU-01 | P1 | Claude / ChatGPT UI | ENG-03 | Batch UI + persistente Jobqueue, Stop/Resume/Error-report |
| STU-02 | P1 | Claude | STU-01/ENG-04 | 20-Clip Batchabnahme, Qualitäts-/Provenance-Reports |
| STU-03 | P1 | Owner + Rechts-/Steuerprüfung | STU-02 | Preis, Studio-Scope, Store, Terms/Refund/VAT final |
| STU-04 | P1 | Claude | STU-03 | Offline-fähige Entitlement-Gates ohne Free-Safety-Einschränkung |
| STU-05 | P1 | ChatGPT Website / Claude | STU-04 | Studio-Landingpage, ehrliche Featureliste, Kauf-/Upgrade-Flow, Supporter-Pass |
| STU-06 | P1 | Owner/Claude | STU-05 | Release/QA/Support-Go-Gate, Studio publizieren |
| GROW-01 | P2 | Owner | STU-06 | Paid Conversion, Supportaufwand, Feedback und Qualitätsmetriken prüfen, OEM-Probes nur bei Nachfrage |

**Rollen:** Claude führt Kern/Tests/Release/Wiring und überprüft meine Artefakte. ChatGPT macht CSS/Visuals/Dokumente/Website auf eigenen Branches. Owner entscheidet Preis, Lieferumfang, Datenschutz-/Lizenzänderungen. Kein Push auf `claude/*`.

## 9. Definition of Done – keine leeren Hüllen

**Free Release:**
- Build lauffähig; 1366×768 und 1920×1080 Screenshots tatsächlich aus dem Wails-Build; alle fünf visuellen Bereiche erreichbar und reale Buttons funktionieren.
- Goldens/CI grün; funktionale Create->Master->Export->Play Kette, Error-/Cancel-/Fallbackpfade getestet.
- Website/Binaries, Lizenz/EULA/Changelog und README nennen identische, **bestätigte** Veröffentlichungsstände.
- No hidden paywall, no fabricated status, kein ungewollter Upload privater Daten.

**Studio Release:**
- Ein Video ohne manuelle ROI erzeugt auf Benchmark-Korpus ein durch Quality Doctor validiertes Ergebnis; nicht bestandene Szenen werden ehrlich markiert.
- Adaptive Engine wählt bei schwierigen Szenen reproduzierbar Alternativen; bei schlechtem Ergebnis fällt sie auf Baseline zurück.
- Batch mit echten Persist-/Resume-/Cancel-/Duplicate-Export-/Failure-Tests; kein ungetesteter „Auto“-Knopf.
- KI optional; AI-Draft nur mit Provenance und unabhängigen Prüfungen, sonst kein Ersatz.
- Device-Profile halten Limits in Mock und realem Smoke ein; Not-Stopp funktioniert in Free und Studio.
- Rechtlich geprüfter Studio-Kauf, Rückgabe-, Lizenz- und Supportpfad; optional 40-€-Supporter-Angebot sauber als **Kauf**, nicht als Spende mit garantierter Gegenleistung.

## 10. Offene Owner-Entscheidungen (nicht heimlich entscheiden)

1. **Free-Grenze:** alle aktuell realen Free-Funktionen dauerhaft erhalten? *Konzept empfiehlt ja*.
2. **Studio-Einstiegspreis:** 49 € Einmalkauf als vorläufiger Testpreis vs anderer Betrag; *noch nicht beschlossen*.
3. **40-€-Supporter:** bevorzugte rechtlich klare Kaufversion ab Studio-Launch oder später freiwilliges Geschenk? **Kein automatischer Spende→Lizenz-Mechanismus unter aktueller EULA.**
4. **Studio Upgrade-Politik:** Updates für eine Hauptversion inkl. Sicherheitsfixes, künftige Major-Upgrades optional kostenpflichtig?
5. **Releasezeitpunkt:** nur nach tatsächlichem FREE-07-Abnahmeprotokoll, nicht nach Kalenderdatum.

**Bevor neue Architektur-/Monetarisierungscodearbeit beginnt:** Dieses Konzept in Issue #477 prüfen, abweichende ältere Roadmaptexte als historisch kennzeichnen, und pro Arbeitspaket eigene PRs mit Benchmark/Test/Abnahme erstellen.


## 11. OWNER ADDENDUM — internationaler Markt, verpflichtender Studio-Zugang nach Unterstützung >40 € (12.10.2026)

**Diese neuere Owner-Vorgabe übersteuert abweichende frühere Preis-/Supporter-Vorschläge in diesem Dokument.** Free dauerhaft weiterentwickeln; Studio zusätzlich, nicht Ersatz. Weltweit/englischsprachig vermarkten, analog zur Marktpräsenz von FunGen 2, **ohne** dessen Code/Assets zu übernehmen. Mehr als 40 € Unterstützung **berechtigen verbindlich** zum Studio-Zugang, sobald Studio veröffentlicht ist. Exakter Schwellenwert ist **> 40,00 EUR**, nicht >= 40,00; 40,00 EUR allein reicht nach wörtlicher Owner-Regel nicht. Vor Produktivstart in Marketing/Kaufbedingungen verständlich klarstellen, falls Owner die Schwelle in eine runde 40-€-Option ändern möchte. Bis Studio verfügbar ist: keine falsche Sofortzugangszusage, ein klar dokumentierter Einlöseanspruch erst ab Start; keine Vorverkaufszahlung ohne geklärte Lieferbedingungen und Verbraucherschutz.

### 11.1 Studio-Funktionen nach Abhängigkeiten / Nachweis

| ID | Priorität | Studio-Feature | Technischer Leistungsumfang und Abnahme | Abgrenzung Free |
|---|---|---|---|---|
| STUDIO-A1 AutoDirector | P0 | Auto-Verfahrenswahl pro Szene | Fast Scan, Kandidatenranking mit Gründe-Codes, selective retry und Golden-Clip-Report; verifiziert gegen heutige Baseline | Free klassisches Create bleibt |
| STUDIO-A2 Smart Batch | P0 | Mehrere Videos sicher automatisiert bearbeiten | persisted queue, Multi-Preset, eingeschränkte parallele CPU/GPU-Auslastung, Clip/Scene-Retry, crash resume, progress/error JSON | Bestehende einfache Queue in Free nicht entfernen |
| STUDIO-A3 Quality Compare | P0 | Klassische/AI/neue Kandidaten vergleichen | zwei Kurven, Scene-Scores, Changed-Segments, undo/accept/reject; keine Verbesserung ohne Gate | Free Quality Doctor bleibt |
| STUDIO-A4 Device Optimizer | P0 | Master-Script je Device projizieren | capability match, safe limits, Multi-Export, Preview mit Mock-Device, keine erfundenen Achsen | Free Gerätsteuerung/Not-Stopp bleibt |
| STUDIO-A5 Intelligent Repair | P0 | Problemsegmente reparieren | targeted re-run, loss/jitter/PTS/camera QA, accept only on measured win, fallback always available | Free Grundglättung bleibt |
| STUDIO-A6 Recovery | P0 | State/Job Resume | restart across interrupted export/job, atomic results, no orphan state, clear recovery UI | Free darf keinen Datenverlust haben |
| STUDIO-B1 Scene Studio | P1 | Region/szene sperren und ersetzen | locks, mark provenance, scene-only rerun, version history, undo | Free Scene Map/Editor bleibt |
| STUDIO-B2 Multi-target | P1 | mehrere Zielpunkte verfolgen | stable IDs, Occlusion, no silent identity switch, explicit target chosen | Free einzelne ROI bleibt |
| STUDIO-B3 Local Learning | P1 | lernende automatische Verfahrenswahl | consent, data deletion, shadow mode, measured model gains before default use | Free persönliche Daten bleiben lokal |
| STUDIO-B4 Pro Exports | P1 | Report + Device-Profile | batch JSON/CSV quality, per-axis companion export, device map manifests | Free Funscript-Export bleibt |
| STUDIO-B5 Hardware Profiler | P1 | Engine auto tuning | 30/60fps & VFR speed/quality measurements, cache + tuning profiles, no secret uploads | Free grundlegende Performance |
| STUDIO-C1 VR | P2 | 180/360 stereoskopische Analyse | camera projection, target ROI continuity, dedicated golden clips/quality gate | erst nach echter Testmatrix |

**Nicht als Studio-Feature verkaufen:** bloßer KI-Chat, Fake-Confidence, leere Presets, nicht verdrahtete Buttons, angebliche universelle Unterstützung ohne Geräte-Mocks.

### 11.2 Internationales Angebot / Wettbewerb

**Referenz zum heutigen Markt:** FunGen 2 zeigt öffentlich Free, Pro 1 Jahr 75 EUR, Pro Perpetual 250 EUR und mehrere Zahlungswege. Diese Zahlen sind **Wettbewerbsbeobachtung**, keine Studio-Preisfreigabe. SamNPlayer darf keine Überlegenheit versprechen, bevor Vergleichstests auf denselben Clips und vergleichbarer Hardware durchgeführt wurden.

- **Website EN zuerst:** sprachneutrale Screenshots aus realem Build, Feature-Matrix Free vs Studio, klare Scope-/Hardware-/AI-Voraussetzungen, Datenschutz „local first“, Download/Changelog/FAQ und öffentliche Limitations.
- **Optional DE:** lokalisierte zweite Seite erst nach stabiler EN-Version; Preise in EUR plus verständliche Umrechnung bei Checkout; Steuer-/Ländervorgaben des Anbieters gelten.
- **Studio Unique Value Proposition:** „Classical motion tracking + optional local AI + measured verification + device-aware export + reliable automation“, bewusst keine reine AI-Cloud-Dienstleistung.
- **Legal/age:** nur für Erwachsene; Transparenz zu Geräten/Content; internationaler Verkauf benötigt regionale Regeln, Refund/Terms, Tax/VAT, Händler-/Payment-Eignung und Anbieterprüfung. **Stripe ist für viele Adult-Content-/Adult-Produkt-Kategorien nicht verfügbar; keine Integration ohne schriftliche Eignungsbestätigung.**
- **Distribution:** offizielle öffentliche Site als Download-Ort, privates Source-Repo, signierte Offline-Entitlements ohne Cloud-Zwang, keine fremden Modelweights in Paketen ohne Lizenzprüfung.

### 11.3 >40-EUR-Unterstützerrecht — gewünschtes verbindliches Verhalten

**Produktregel:** Nach bestätigter, nicht erstatteter qualifizierender Unterstützung **von mehr als 40,00 EUR** ist ein **Studio-Zugangsanspruch** zu vergeben. Für Beiträge vor Verfügbarkeit von Studio muss die zukünftige Leistung klar beschrieben sein (betroffene Studio-Edition, Dauer, Upgrade-Rechte, Zeitpunkt/Abhängigkeiten, Refund bei Nichtlieferung); vor finalen Bedingungen darf der Anspruch nicht öffentlich beworben oder Zahlung dafür angenommen werden.

**Rechtliche Einordnung:** Eine Zahlung mit verbindlicher Lizenzgegenleistung ist regelmäßig kein rein freiwilliges Geschenk; die Benennung „Spende“ ändert den wirtschaftlichen Vorgang nicht. Die bestehende EULA §6 („Donations ... do not buy a licence, extra features“) widerspricht einem garantierten Studio-Recht. Erforderliche Umsetzung vor Aktivierung: angepasste Terms/EULA, korrekte Rechnungen/Steuern, Verbraucherrechte, Zahlungsanbieter der den Adult-nahen Software-Use-Case ausdrücklich akzeptiert. Separate weiterhin freiwillige Spenden ohne Gegenleistung bleiben möglich.

**Entitlement-Datenmodell (nur Konzept, keine Implementierung oder personenbezogenen Daten im Repo):**

```text
SupporterEntitlement {
  id: UUID,
  payment_provider: string,
  payment_reference_hash: string,
  amount_received_minor: int64, currency: ISO4217,
  amount_eur_reference_minor: int64,
  currency_conversion_source: string,
  settled_at: UTC,
  status: pending | qualified | issued | refunded | revoked,
  product: studio,
  edition_scope: string,
  license_term: string,
  user_contact_reference: string,
  pass_serial: string,
  issued_at: UTC | null
}
```

**Spezifikation:**
1. Checkout/Payment event **serverseitig signiert** und idempotent verifizieren, nicht nur Browser Return-URL.
2. Währung zum vereinbarten Zeitpunkt nach dokumentierter Referenz in EUR umrechnen; Gebührenfrage und Schwelle transparent anzeigen, Grenzfälle (40,00 / 40,01 EUR) testen.
3. Auf bestätigte Zahlung + Betrag >40 EUR prüfen, Rückerstattung/Chargeback behandeln; mehrfach ausgelöste Webhooks erzeugen keine Duplikate.
4. Studio erst **nach Veröffentlichung**, dann signierten lokal verifizierbaren Pass ausgeben (Ed25519 Public Key in App; geheimen Private Key nur beim Betreiber); Eigentümer kann verlorenen Pass erneut anfordern; kein „Online every launch“.
5. Automatik: Pass per E-Mail/Account-Portal nach Bestätigung, Re-issue/Support + datensparsame Logs; **no filming data transmitted**. Status auch bei Ausfall der Mailzustellung nachvollziehbar.
6. Freiwillige Spenden ohne Anspruch, qualifizierender Supporter-Studio-Beitrag und normaler Studio-Kauf **im Checkout klar unterscheiden**; das kann auf derselben Seite erklärt werden.
7. Vor Implementierung Owner festlegen: Anspruch **zeitlich unbefristet für die erworbene Studio-Hauptversion** (empfohlener Default, zukünftige Major-Upgrades nicht automatisch enthalten) oder Lifetime inklusive künftiger Versionen; kein stillschweigend erfundener Lifetime-Anspruch.
8. **Sicherheit:** Pass-Prüfung backendseitig; Free-Player/Not-Stopp/Sicherheitsgrenzen bleiben immer frei; Testmatrix Offline, Uhrzeitfehler, Mehrfachaktivierung, Refund, Editionsupdate.

### 11.4 Separate Studio-Launch-Gates

- **Commercial:** internationale EN-Rechtstexte und rechtssicherer qualifizierender >40-€-Supporter-Vertrag; Support-/Return-/Tax/Provider bestätigt; no unapproved marketing claims.
- **Product:** P0 A1–A6 mit Tests; Free unverändert; Demo-Clip-Produktion ohne reale intime Medien in öffentlichem Repo.
- **Technical:** native CPU/GPU-Baselines, Golden-Curve-Metriken, Crash/Resume, signed entitlement/renewal, Windows offline smoke.
- **Competition:** FunGen-Vergleich ausschließlich anhand nachvollziehbarer Prüfungen, nicht bloß Feature-Liste.

## 12. Studio Editor / competitive blueprint (Owner 12 Oct)

**New companion specification:** [STUDIO_EDITOR_COMPETITIVE_BLUEPRINT_2026-10-12.md](STUDIO_EDITOR_COMPETITIVE_BLUEPRINT_2026-10-12.md). The Owner requires a **full-featured, frame-accurate multi-axis editor**, not just curve display: dynamic script/device lanes, true VFR/PTS timeline, selected-axis multi-point editing, selection tools, range/multi-axis transactions, undo/redo history, transforms, ghost/diff overlays, quality/repair inspector, independent device preview, compatible .funscript companion axes and lossless .samn migration. EDIT-00..08 tickets and ED-01..10 acceptance tests are defined in that document. This expands Studio scope and the competitive bar, but **must not delay the Free release or remove today's Free editing features**.

**Competitive research note (2026-10-12):** FunGen currently publicly advertises frame-accurate multi-axis editing, history, transforms, Funscript Doctor, batching, AI; OpenFunscripter is GPL-3 (study UX, no proprietary code copy); MultiFunPlayer is MIT (study protocols and possible licensed import after dependency notice audit). See verified primary-source links and decision matrix in companion blueprint. “Best in market” is the owner's goal, not an unsubstantiated release claim.
