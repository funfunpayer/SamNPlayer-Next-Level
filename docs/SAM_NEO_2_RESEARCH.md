# Sam Neo 2 – technische Recherche für SamNPlayer

Pasted by the user on September 15, 2026, as a standalone research
document (own wording kept as-is, only reformatted into Markdown; no
content added or removed except the cross-reference note in §9a below,
clearly marked as an addition). Follows the same pattern as
`SAM_ARCHITECTURE.md`'s 10-phase vision document: recorded here in full,
then cross-referenced against what this repo already implements.

**Stand:** 15. September 2026
**Zweck:** Technische Wissensbasis für die Geräteintegration in SamNPlayer
**Referenzgerät:** SVAKOM Sam Neo 2, Modell S08P

**Hinweis:** Dieses Dokument trennt Herstellerangaben, regulatorische
Nachweise, Protokoll-/Ökosystem-Dokumentation und Community-Erkenntnisse.
Nicht offiziell bestätigte Eigenschaften müssen am realen Gerät validiert
werden.

## 1. Kurzfazit

Der Sam Neo 2 ist für SamNPlayer interessanter als ein Gerät mit nur
einem einfachen Vibrationsausgang. Die belastbarsten Quellen ergeben zwei
für die Software relevante Effektbereiche:

- Vibration
- Saug-/Constriction-Effekt

SVAKOM beschreibt fünf Saugmodi, fünf Vibrationsmodi und fünf kombinierte
Modi. Die offizielle Buttplug-Spezifikation führt den Svakom Sam Neo 2
ausdrücklich als Beispiel für den Output-Typ Constrict. Ein unabhängiges
Open-Source-Projekt beschreibt beim Sam Neo 2/2 Pro getrennte Aktuatoren
für Vibration und Vacuum/Constrict und verwendet sie unabhängig sowie
kombiniert.

Für SamNPlayer sollte das Gerät deshalb zunächst als
Multi-Channel-Referenzgerät betrachtet werden. Nicht belegt ist dagegen
ein echter linearer Positionsantrieb oder eine Rotation. Eine
Funscript-Position darf daher nicht einfach als reale mechanische
Position des Sam Neo 2 interpretiert werden.

## 2. Quellenbewertung

| Stufe | Quellentyp | Verwendung |
|---|---|---|
| A | SVAKOM / offizielle Herstellerangaben | Produktdaten, Funktionen, Modell |
| A | FCC / regulatorische Unterlagen | Modellidentität, Funkdaten |
| A/B | Buttplug-Spezifikation | Softwareseitige Capability-Abstraktion |
| B | IoST Index | unabhängige Capability-Zusammenfassung |
| C | unabhängige GitHub-Projekte | technische Hinweise, die am Gerät zu verifizieren sind |

Community-Implementierungen sind keine Herstellerdokumentation. Sie sind
für Reverse Engineering und Tests wertvoll, dürfen aber nicht ungeprüft
als Hardware-Spezifikation übernommen werden.

## 3. Offizielle SVAKOM-Angaben

SVAKOM führt das Gerät als SAM NEO 2 – Interactive Sucking & Vibrating
Masturbator.

**Produktdaten:**

| Eigenschaft | Angabe |
|---|---|
| Modell/SKU | S08P |
| Material | TPE, ABS |
| Abmessungen | 228 × 75 × 75 mm |
| maximale Innenmaße der Sleeve | 140 × 45 × 45 mm |
| Gewicht | 711 g |
| Nennspannung | 3,7 V |
| Ladespannung | 5 V |
| Akku | 1000 mAh Lithium-Polymer |
| Ladezeit | ca. 2 h |
| Laufzeit | ca. 1 h |
| Saugmodi | 5 |
| Vibrationsmodi | 5 |
| kombinierte Modi | 5 |
| Gehäuse | nicht wasserdicht |
| herausnehmbare Sleeve | wasserdicht |

SVAKOM bewirbt außerdem Bluetooth-/App-Steuerung, Fernsteuerung,
benutzerdefinierte Einstellungen, interaktive Videoinhalte und den
sogenannten Extended-O-Modus.

- Offizielle Produktseite: https://www.svakom.com/products/sucking-vibrating-masturbator
- Deutsche Produktseite: https://www.svakom.com/de/products/sucking-vibrating-masturbator

## 4. Funk und Modellidentität

Die FCC-Zulassung bestätigt:

- Produkt: Sam Neo 2
- Modell: S08P
- FCC ID: 2A74F-S08P
- Antragsteller: Shenzhen Svakom Technology Co., Ltd.
- Frequenzbereich: 2402–2480 MHz
- Zulassungsdatum: 3. Juli 2024

Die FCC-Akte enthält unter anderem Benutzerhandbuch, interne und externe
Fotos, Testberichte, Antennenbericht und RF-Unterlagen.

FCC-Akte: https://fccid.io/2A74F-S08P

Das Benutzerhandbuch nennt ebenfalls Modell S08P und bestätigt den
Funkbetrieb im Bereich 2402–2480 MHz sowie die Konformität nach
EU-Richtlinie 2014/53/EU.

## 5. Buttplug-Unterstützung

Für SamNPlayer besonders wichtig ist die offizielle
Buttplug-Dokumentation.

In der aktuellen OutputCmd-/OutputType-Dokumentation wird Svakom Sam Neo
2 ausdrücklich als Gerätebeispiel für Constrict aufgeführt.

Constrict besitzt einen vom Gerät gemeldeten Wertebereich. Die Software
sollte deshalb nicht von einer fest angenommenen Zahl von Stufen
ausgehen, sondern die tatsächlich gemeldeten Device Features und
Wertebereiche auslesen.

Buttplug OutputCmd / OutputType: https://buttplug.io/docs/spec/output/

Der IoST-Änderungsverlauf dokumentiert außerdem, dass der Sam Neo 2 seit
2024 als von Buttplug-Rust unterstützt geführt wird. Ein Update von Juni
2026 weist dem Sam Neo 2 zudem eine Buttplug-UUID zu.

## 6. Unabhängige Capability-Daten

Der IoST Index ist keine Herstellerquelle, aber für die technische
Gegenprüfung hilfreich.

Für Sam Neo 2 bzw. die Produktfamilie wird das Gerät als Bluetooth-Gerät
mit Vibrations- und Grip/Expander-/Constriction-artiger Funktion
eingeordnet. Beim Sam Neo 2 Pro werden ausdrücklich folgende Outputs
aufgeführt:

- 1 Vibrator
- 1 Grip/Expander
- 1 Heater
- keine positionalen linearen Aktuatoren
- keine oszillierenden linearen Aktuatoren
- keine Rotation

Für SamNPlayer ist besonders relevant, dass diese Daten keinen
klassischen Positionsmotor nahelegen.

- IoST Index: https://iostindex.com/
- Sam Neo 2 Pro Capability-Seite: https://iostindex.com/devices/svakom/sam%20neo%202%20pro/

## 7. Community-/Reverse-Engineering-Hinweise

Das unabhängige Projekt Kyure-A/mcp-svakom-samneo beschreibt für Sam Neo
2 / Sam Neo 2 Pro:

- getrennte Aktuatoren für Vibration und Vacuum/Constrict,
- Vacuum-only-Steuerung,
- kombinierte Vibration-/Vacuum-Steuerung,
- synchrone Muster,
- alternierende Muster,
- unabhängige Muster.

Das ist für unsere Forschung sehr interessant, aber nicht als offizielle
SVAKOM-Protokollspezifikation zu behandeln. Jede Aussage daraus muss
gegen den realen Sam Neo 2 getestet werden.

Projekt: https://github.com/Kyure-A/mcp-svakom-samneo

## 8. Was als gesichert gelten kann

**Hohe Sicherheit**

1. Der Sam Neo 2 ist Modell S08P.
2. Bluetooth/Funkkommunikation ist vorhanden.
3. Der Frequenzbereich 2402–2480 MHz ist regulatorisch dokumentiert.
4. Das Gerät besitzt Vibrationsfunktion.
5. Das Gerät besitzt einen Saug-/Constriction-Effekt.
6. SVAKOM bietet kombinierte Vibrations-/Saugmodi.
7. App- und interaktive Steuerung gehören zum vorgesehenen Nutzungskonzept.
8. Buttplug führt den Sam Neo 2 als Beispielgerät für Constrict.
9. Ein klassischer linearer Positionsantrieb ist derzeit nicht belegt.

**Mittlere Sicherheit – am Gerät validieren**

1. Vibration und Constrict/Vacuum lassen sich vollständig unabhängig und
   gleichzeitig steuern.
2. Beide Kanäle akzeptieren ausreichend fein abgestufte dynamische Werte.
3. Schnelle Rampen und kontinuierliche Wellen sind praktisch nutzbar.
4. Die maximale sinnvolle Befehlsrate reicht für eine hochauflösende
   Echtzeitabbildung.
5. Beide Kanäle können ohne gegenseitige Störung synchronisiert werden.

## 9. Was wir noch nicht wissen

Folgende Werte habe ich nicht belastbar in einer offiziellen
SVAKOM-Spezifikation gefunden:

- BLE-GATT-Services und Characteristics
- offizielle Rohkommandos
- tatsächliche digitale Auflösung von Vibration
- tatsächliche digitale Auflösung von Constrict
- minimale Änderungsschritte
- maximale Befehlsrate
- minimale stabile Befehlsintervalle
- Ende-zu-Ende-Latenz
- Jitter
- Paketverluste
- mechanische Anstiegszeit
- mechanische Abfall-/Release-Zeit
- reale Saug-/Druckkennlinie
- Verhalten bei schnellen Sollwertänderungen
- Kanalinteraktion bei gleichzeitiger Ansteuerung
- Reconnect-Zeit nach BLE-Abbruch
- Verhalten des Geräts bei Timeout oder verlorener Verbindung

Diese Punkte gehören in das SamNPlayer Device Lab und dürfen nicht
geraten werden.

### 9a. Ergänzung (nicht Teil des Originaldokuments): zwei Punkte sind in diesem Repo bereits vorhanden

Die ersten beiden Punkte der obigen Liste – **BLE-GATT-Services und
Characteristics** sowie **offizielle Rohkommandos** – sind nicht mehr
vollständig offen. `device/protocol.go` (`SamNeo2Protocol`) enthält
bereits konkrete Werte, nicht aus SVAKOM-Marketing, sondern direkt aus
dem gepflegten `buttplug-rust`-Quellcode gegengeprüft
(`svakom_sam2.rs`/`svakom-sam2.yml`) – nach diesem Dokuments eigenem
Evidenz-Schema also **PROTOCOL**, nicht nur COMMUNITY:

- Service UUID `0000ffe0-...`, TX-Characteristic `0000ffe1-...` (Kommandos),
  RX-Characteristic `0000ffe2-...` (Notify, ungenutzt).
- Beide Kanäle als 7-Byte-Pakete an dieselbe TX-Characteristic, per
  zweitem Byte unterschieden (`0x03` = Vibrate, `0x09` = Constrict).
- GATT Write **with** Response (bestätigt im Quellcode, ohne
  Initialisierungs-Handshake – anders als das ältere `LegacySamProtocol`
  für den Original-Sam-Neo).
- Wertebereiche laut `svakom-sam2.yml`: Vibration 0–10, Constrict 0–5 –
  markiert im Code selbst als StepCount-Angabe aus der
  Buttplug-Gerätekonfiguration, **nicht** notwendigerweise eine
  Firmware-Grenze (`EncodeVibrationRaw`/`EncodeSuctionRaw` existieren
  bewusst, um das an echter Hardware zu klären – deckt sich mit diesem
  Dokuments eigener MEASURED-vs-HYPOTHESIS-Unterscheidung in §18).
- `device/samneo2.go`: Constrict nutzt laut Buttplug-Rust-Quellcode
  (`output_cmd.rs`) denselben generischen Skalar-Werttyp wie Vibrate,
  nur mit weniger Auflösung – keine feste Preset-Auswahl. Eine frühere,
  falsche Annahme (gestützt auf einen Forenbericht zur SVAKOM-App, nicht
  auf das rohe Protokoll) wurde bereits korrigiert.

Damit bleiben von der obigen Liste offen: digitale Auflösung real
bestätigen, minimale Änderungsschritte, Befehlsrate, Latenz/Jitter,
Anstiegs-/Abfallzeit, Kanalinteraktion, Reconnect-Verhalten – exakt der
Rest der Liste, plus die Frage, ob die Buttplug-Rust-Werte (0–10, 0–5)
tatsächlich die Firmware-Grenze sind oder nur die App-UI-Grenze. Nichts
davon ist ohne echtes Gerät zu klären; siehe `docs/NEXT.md` Priorität 1.

## 10. Zielmodell für SamNPlayer

Der Sam Neo 2 sollte zunächst nicht als simples Gerät mit „Vibration +
Saugen" modelliert werden, sondern als messbares Multi-Channel Reference
Device.

**Wahrnehmungsebene**

SamNPlayer analysiert unter anderem:

- Bewegung
- Rhythmus
- Geschwindigkeit
- Beschleunigung
- Richtungswechsel
- Bewegungsumfang
- Intensitätsdynamik
- Szenenkontext
- Confidence

**Motion Engine**

Die Motion Engine erzeugt daraus eine geräteunabhängige
Bewegungs-/Effektabsicht.

**Device Response Model**

Erst anschließend übersetzt das Sam-Neo-2-Geräteprofil diese Absicht auf
die tatsächlich verfügbaren Fähigkeiten:

- Vibration
- Constrict/Suction
- zeitliche Koordination
- gerätespezifische Grenzen
- Latenzkompensation
- Response-Kurven

Dadurch bleibt die übergeordnete TF-/Motion-Logik unabhängig vom
konkreten Gerät und spätere Geräte können über eigene Capability- und
Response-Profile ergänzt werden.

## 11. Device-Characterization für den echten Sam Neo 2

Für eine professionelle Integration sollte SamNPlayer eine
reproduzierbare Messreihe vorsehen.

**Verbindung** – messen: Scan-Zeit, Connect-Zeit, Disconnect-Verhalten,
Reconnect-Zeit, Stabilität über längere Sitzungen, Verhalten nach
App-/Intiface-Wechsel.

**Vibration** – messen: tatsächlich akzeptierte Werte/Stufen, kleinste
wahrnehmbare bzw. physikalisch messbare Änderung, Anstiegszeit,
Abfallzeit, maximale stabile Update-Rate, Reaktion auf Rampen, Reaktion
auf kurze Pulse, Wiederholgenauigkeit.

**Constrict/Suction** – messen: tatsächlich akzeptierte Werte/Stufen,
reale Wirkung je Sollwert, Anstiegszeit, Release-Zeit, maximale stabile
Update-Rate, Rampen, Pulse, Wellen, Wiederholgenauigkeit.

**Kanalinteraktion** – prüfen: Vibration allein, Constrict allein, beide
gleichzeitig, synchrone Änderung, zeitversetzte Änderung, schnelle
Wechsel, mögliche gegenseitige Beeinflussung, Priorisierung/Überschreiben
von Befehlen.

**Transport** – pro Kommando protokollieren: monotone Zeit, gewünschter
Wert, tatsächlich gesendeter Wert, Kanal, Transport, Verbindungsstatus,
Fehler, Wiederholungen, beobachtete Antwort, sofern messbar.

*Automatisiert in `device/diagnostics.go`, bedienbar über den "Geräte-
Diagnose"-Abschnitt im Geräte-Tab: Rohwert-Annahme pro Kanal, maximale
stabile Update-Rate, Kanalinteraktion (allein/gleichzeitig/zeitversetzt/
schnelle Wechsel), vollständiges Transport-Log als JSONL-Verlauf. Deckt
genau das ab, was sich ohne einen Sensor am Gerät objektiv messen lässt
(Schreib-Latenz, angenommene Werte, Fehler) - NICHT die gefühlte
Intensität oder die echte physische Anstiegs-/Abfallzeit weiter unten in
§12, die bleiben Sache des Nutzers am echten Gerät.*

## 12. Device Response Model

Nach der Messung soll nicht nur eine Liste unterstützter Funktionen
entstehen, sondern ein echtes Geräteprofil.

**Vibration** – speichern: Wertebereich, Auflösung, Response-Kurve,
Latenz, Rise Time, Fall Time, maximale stabile Update-Rate, empfohlene
Update-Rate.

**Constrict** – speichern: Wertebereich, Auflösung, Response-Kurve,
Latenz, Rise Time, Release Time, maximale stabile Update-Rate, empfohlene
Update-Rate.

**Gemeinsames Verhalten** – speichern: simultane Steuerbarkeit,
Synchronisationsfehler, Kanalinteraktion, sichere Grenzwerte,
Timeout-Verhalten, Stop-Verhalten, Reconnect-Verhalten.

## 13. Konsequenz für Funscript

Ein `.funscript` beschreibt typischerweise eine Positionskurve. Der Sam
Neo 2 besitzt nach dem derzeitigen Kenntnisstand jedoch keinen
entsprechenden linearen Positionsmotor.

Deshalb darf SamNPlayer nicht einfach:

> Funscript Position → Sam-Neo-2-Position

abbilden.

Stattdessen:

> Funscript/Video → SAM Motion Representation → Device Mapping →
> Vibration + Constrict

Die Motion Engine interpretiert also Dynamik, Rhythmus, Richtung,
Geschwindigkeit und Intensität und übersetzt diese anschließend
gerätespezifisch.

Das ist ein zentraler Architekturpunkt für SamNPlayer.

## 14. Sam Neo 2 vs. Sam Neo 2 Pro

Die Geräte dürfen nicht verwechselt werden.

**Sam Neo 2**
- Modell: S08P
- Vibration
- Saug-/Constriction-Effekt
- Bluetooth/App
- keine belegte Heizung

**Sam Neo 2 Pro**
- Modell: S08PRO
- Vibration
- Grip/Expander-/Constriction-Funktion
- zusätzliche Heizung
- IoST weist darauf hin, dass die Heizungssteuerung manuell ist

Die Heizung darf daher nicht automatisch als frei programmierbarer
SamNPlayer-Kanal angenommen werden.

## 15. Architekturentscheidung für SamNPlayer

Sam Neo 2 wird zunächst Referenzgerät für die
Multi-Channel-Geräteabstraktion.

Die Architektur soll jedoch niemals fest auf dieses eine Modell
verdrahtet werden.

Geräte werden über Fähigkeiten beschrieben, beispielsweise:

- Vibration
- Constrict
- Suction
- Position
- Rotation
- Oscillation
- Heating
- weitere zukünftige Outputs

SamNPlayer erkennt die verfügbaren Fähigkeiten und wählt ein passendes
Device Mapping.

Der Sam Neo 2 ist damit erstes Kalibrierungs- und Referenzgerät, nicht
die Grenze der Architektur.

## 16. Empfohlene nächste Schritte

1. Sam Neo 2 S08P mit Intiface/Buttplug verbinden.
2. Tatsächlich gemeldete Device Features und Wertebereiche protokollieren.
3. Vibration und Constrict einzeln testen.
4. Gleichzeitige Steuerung validieren.
5. reale Update-Rate, Latenz und Jitter messen.
6. Stop und Reconnect testen.
7. Messreihe wiederholen und Reproduzierbarkeit bestimmen.
8. daraus SamNeo2DeviceProfile bzw. das äquivalente geräteunabhängige
   Profil ableiten.
9. erst danach optimierte Motion-to-Device-Mappings trainieren bzw.
   abstimmen.
10. Community-Erkenntnisse nur übernehmen, wenn sie durch reale Tests
    bestätigt wurden.

Alle zehn Schritte brauchen ein reales Gerät und einen Operator – siehe
`docs/NEXT.md` Priorität 1 ("Validate real hardware").

## 17. Quellen

**Hersteller / offiziell**
- SVAKOM – Sam Neo 2: https://www.svakom.com/products/sucking-vibrating-masturbator
- SVAKOM – Sam Neo 2 deutsch: https://www.svakom.com/de/products/sucking-vibrating-masturbator

**Regulatorisch**
- FCC – Sam Neo 2, FCC ID 2A74F-S08P: https://fccid.io/2A74F-S08P
- FCC – SVAKOM Geräteübersicht: https://fccid.io/2A74F

**Protokoll / Geräteabstraktion**
- Buttplug – OutputCmd und OutputType: https://buttplug.io/docs/spec/output/

**Unabhängige Capability-Daten**
- IoST Index: https://iostindex.com/
- IoST – Sam Neo 2 Pro: https://iostindex.com/devices/svakom/sam%20neo%202%20pro/

**Community / technische Exploration**
- Kyure-A – mcp-svakom-samneo: https://github.com/Kyure-A/mcp-svakom-samneo

## 18. Dokumentationsregel

Neue Erkenntnisse über den Sam Neo 2 werden künftig mit einem
Evidenzstatus versehen:

- **OFFICIAL** – Herstellerangabe
- **REGULATORY** – regulatorischer Nachweis
- **PROTOCOL** – dokumentierte Protokoll-/Capability-Angabe
- **COMMUNITY** – unabhängige Implementierung/Beobachtung
- **MEASURED** – mit unserem realen Gerät reproduzierbar gemessen
- **HYPOTHESIS** – noch nicht bestätigt

Für produktive Geräteprofile sollen MEASURED-Daten Vorrang vor
Marketingangaben und Community-Annahmen haben.

---

**Leitgedanke:** SamNPlayer soll nicht raten, was ein Gerät kann. Es soll
Fähigkeiten erkennen, reale Geräteantworten messen und seine Ausgabe an
das tatsächlich beobachtete Verhalten anpassen.
