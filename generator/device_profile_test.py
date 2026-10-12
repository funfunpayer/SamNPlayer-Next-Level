"""Regressionstest für die Geräteverträglichkeit.

Die Grenzwerte stammen aus den etablierten Werkzeugen der
Funscript-Gemeinschaft, nicht aus eigener Anschauung - siehe die Quellen im
Kopf von device_profile.py. Sie betreffen nicht unser eigenes Gerät, sondern
die erzeugte Datei: sie soll auch auf fremder Hardware brauchbar sein.

Besonders wichtig ist die Gegenrichtung: ein normales, gut gemachtes Skript
darf KEINE dieser Warnungen auslösen. Eine Verträglichkeitsprüfung, die bei
brauchbaren Skripten anschlägt, wird ignoriert und ist damit wertlos.

Ausführen:  python3 generator/device_profile_test.py
"""

import sys

import numpy as np

import device_profile


def script(step_ms, amplitude=100.0, offset=0.0, period_ms=1000.0, duration_ms=20000):
    at = np.arange(0, duration_ms, step_ms, dtype=float)
    pos = (np.sin(2 * np.pi * at / period_ms) + 1) / 2 * amplitude + offset
    return [{"at": int(a), "pos": int(round(p))} for a, p in zip(at, pos)]


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- Ein normales Skript darf nicht anschlagen ------------------------
    normal = script(step_ms=250)
    metrics, warnings = device_profile.evaluate(normal)
    check("normales Skript löst keine Warnung aus", not warnings, str(warnings))
    check("voller Positionsbereich wird erkannt",
          metrics["position_span"] > 95, str(metrics["position_span"]))

    # --- Intensität nach der Definition aus funscript-utils ---------------
    # 500 * |Δpos| / |Δt|. Ein voller Hub (100) in einer Sekunde ergibt
    # per Definition den Wert 100 - das ist die Probe auf die Formel.
    one_stroke_per_second = [{"at": 0, "pos": 0}, {"at": 500, "pos": 100},
                             {"at": 1000, "pos": 0}]
    values = device_profile.intensity(one_stroke_per_second)
    check("ein voller Hub pro Sekunde ergibt Intensität 100",
          all(abs(v - 100.0) < 0.01 for v in values), str(values))

    half = [{"at": 0, "pos": 20}, {"at": 300, "pos": 60}]
    check("Beispiel aus der Referenz: 20->60 in 300ms ergibt ~67",
          abs(device_profile.intensity(half)[0] - 66.7) < 0.5,
          str(device_profile.intensity(half)[0]))

    # --- Zu dichte Actions -------------------------------------------------
    dense = script(step_ms=40)
    metrics, warnings = device_profile.evaluate(dense)
    check("zu dicht aufeinanderfolgende Actions werden gezählt",
          metrics["actions_too_close"] > 0, str(metrics["actions_too_close"]))
    check("und gemeldet", any("aus dem Takt" in w for w in warnings), str(warnings))

    # Genau an der Grenze darf es NICHT anschlagen.
    at_limit = script(step_ms=device_profile.MIN_INTERVAL_MS)
    metrics, _ = device_profile.evaluate(at_limit)
    check("genau am Mindestabstand wird nicht gemeldet",
          metrics["actions_too_close"] == 0, str(metrics["actions_too_close"]))

    # --- Zu schwache Amplitude --------------------------------------------
    weak = script(step_ms=250, amplitude=30, offset=35)
    metrics, warnings = device_profile.evaluate(weak)
    check("zu geringer Positionsbereich wird gemeldet",
          any("Bereich" in w for w in warnings), str(warnings))

    # --- Zu langsame Vollhübe ---------------------------------------------
    # Große Sprünge mit sehr langem Abstand: das Gerät fährt sie schneller
    # und steht dann still.
    slow = [{"at": 0, "pos": 0}, {"at": 2000, "pos": 100},
            {"at": 4000, "pos": 0}, {"at": 6000, "pos": 100}]
    metrics, warnings = device_profile.evaluate(slow)
    check("zu langsame Vollhübe werden erkannt",
          metrics["slow_full_strokes"] > 0, str(metrics["slow_full_strokes"]))
    check("und gemeldet", any("still" in w for w in warnings), str(warnings))

    # Gegenprobe: kleine Bewegungen über lange Zeit sind normal und dürfen
    # nicht als "zu langsamer Hub" gelten.
    gentle = [{"at": i * 2000, "pos": 50 + (10 if i % 2 else -10)} for i in range(8)]
    metrics, warnings = device_profile.evaluate(gentle)
    check("langsame KLEINE Bewegungen gelten nicht als zu langsamer Hub",
          metrics["slow_full_strokes"] == 0, str(metrics["slow_full_strokes"]))

    # --- Randfälle ---------------------------------------------------------
    metrics, warnings = device_profile.evaluate([{"at": 0, "pos": 50}])
    check("ein einzelnes Action wird ohne Absturz behandelt",
          metrics == {} and warnings == [])

    duplicate = [{"at": 100, "pos": 0}, {"at": 100, "pos": 100}]
    metrics, _ = device_profile.evaluate(duplicate)
    check("gleiche Zeitstempel führen nicht zu Division durch Null",
          all(np.isfinite(v) for v in device_profile.intensity(duplicate)))

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
