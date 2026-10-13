"""Regressionstest für das Backend-Register.

Das Register ersetzt eine fest verdrahtete Fallunterscheidung. Sein Wert
steht und fällt mit dem Vertrag: Ein Plugin, das sich nicht daran hält, muss
SOFORT mit einer verständlichen Meldung scheitern. Täte es das nicht, fiele
der Fehler erst viel später auf - als unerklärlich schlechtes Skript, nicht
als Fehler im Plugin. Das ist der Unterschied zwischen einer offenen
Schnittstelle und einer Einladung zu unauffindbaren Fehlern.

Ausführen:  python3 generator/backends_test.py
"""

import os
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import backends


def gutes_backend(video_path, roi, options):
    n = 100
    timestamps = np.arange(n, dtype=float) * 40.0
    positions = 120 + 40 * np.sin(2 * np.pi * timestamps / 1000.0)
    return timestamps, positions, (320, 240), [], {
        "tracker_lost_frames": 0,
        "total_frames": n,
        "vertical_range": float(np.ptp(positions)),
    }


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    def erwarte_fehler(name, func, textteil):
        backends._BACKENDS.pop("pruef", None)
        backends.register("pruef", func, quelle="test")
        try:
            backends.run("pruef", "video.mp4", (0, 0, 10, 10), {})
            check(name, False, "kein Fehler ausgelöst")
        except backends.BackendError as exc:
            check(name, textteil.lower() in str(exc).lower(), str(exc))

    backends.register("test_gut", gutes_backend, "Testverfahren", quelle="test")
    check("angemeldetes Backend erscheint in der Liste", "test_gut" in backends.available())
    check("Beschreibung und Quelle werden mitgeführt",
          backends.available()["test_gut"]["quelle"] == "test")

    result = backends.run("test_gut", "video.mp4", (0, 0, 10, 10), {})
    check("ein vertragstreues Backend läuft durch", len(result) == 5)
    check("frame_size kommt als Tupel zurück", result[2] == (320, 240))

    try:
        backends.run("gibtsnicht", "v.mp4", (0, 0, 1, 1), {})
        check("unbekanntes Backend wird abgelehnt", False)
    except backends.BackendError as exc:
        check("unbekanntes Backend wird abgelehnt", "Verfügbar" in str(exc), str(exc))

    try:
        backends.register("test_gut", gutes_backend, quelle="anderes_plugin")
        check("doppelter Name wird abgelehnt", False)
    except backends.BackendError:
        check("doppelter Name wird abgelehnt", True)

    try:
        backends.register("kaputt", "keine Funktion", quelle="test")
        check("Nicht-Funktion wird abgelehnt", False)
    except backends.BackendError:
        check("Nicht-Funktion wird abgelehnt", True)

    erwarte_fehler("zu wenige Rückgabewerte",
                   lambda v, r, o: (np.zeros(5), np.zeros(5)), "5 Werte")
    erwarte_fehler("ungleich lange Zeitstempel und Positionen",
                   lambda v, r, o: (np.zeros(10), np.zeros(5), (320, 240), [],
                                    {"tracker_lost_frames": 0, "total_frames": 10,
                                     "vertical_range": 1.0}), "gleich lang")
    erwarte_fehler("fehlender stats-Eintrag",
                   lambda v, r, o: (np.arange(10.0), np.arange(10.0), (320, 240), [],
                                    {"tracker_lost_frames": 0, "total_frames": 10}),
                   "vertical_range")
    erwarte_fehler("stats ist kein dict",
                   lambda v, r, o: (np.arange(10.0), np.arange(10.0), (320, 240), [], "nix"),
                   "dict")
    erwarte_fehler("falsches frame_size",
                   lambda v, r, o: (np.arange(10.0), np.arange(10.0), 320, [],
                                    {"tracker_lost_frames": 0, "total_frames": 10,
                                     "vertical_range": 1.0}), "frame_size")
    erwarte_fehler("zu wenige Messwerte",
                   lambda v, r, o: (np.array([0.0]), np.array([1.0]), (320, 240), [],
                                    {"tracker_lost_frames": 0, "total_frames": 1,
                                     "vertical_range": 1.0}), "zu wenige")

    def wirft(video_path, roi, options):
        raise ValueError("etwas ging schief")

    erwarte_fehler("Absturz im Plugin wird als Backend-Fehler gemeldet", wirft, "gescheitert")

    with tempfile.TemporaryDirectory() as tmp:
        plugin_dir = Path(tmp)
        (plugin_dir / "mein_backend.py").write_text(
            "import numpy as np\n"
            "from backends import register\n"
            "\n"
            "def analyze(video_path, roi, options):\n"
            "    n = 50\n"
            "    t = np.arange(n, dtype=float) * 40.0\n"
            "    pos = 100 + 20 * np.cos(2 * np.pi * t / 800.0)\n"
            "    return t, pos, (640, 480), [], {\n"
            "        'tracker_lost_frames': 0, 'total_frames': n,\n"
            "        'vertical_range': float(np.ptp(pos)),\n"
            "    }\n"
            "\n"
            "register('aus_datei', analyze, 'Aus einer Datei geladen', quelle='plugin')\n")
        (plugin_dir / "kaputt.py").write_text("das ist kein gueltiges Python (")
        (plugin_dir / "_hilfe.py").write_text("register('darf_nicht', None)")

        loaded = backends.load_plugins(str(plugin_dir), verbose=False)
        check("gültiges Plugin wird geladen", "mein_backend.py" in loaded, str(loaded))
        check("kaputtes Plugin wird übersprungen", "kaputt.py" not in loaded, str(loaded))
        check("Hilfsdatei mit Unterstrich wird übersprungen",
              "_hilfe.py" not in loaded and "darf_nicht" not in backends.available())
        check("das geladene Verfahren ist benutzbar", "aus_datei" in backends.available())

        result = backends.run("aus_datei", "v.mp4", (0, 0, 10, 10), {})
        check("es liefert vertragstreue Werte",
              len(result[0]) == 50 and result[2] == (640, 480))
        check("die Quelle wird als Plugin ausgewiesen",
              backends.available()["aus_datei"]["quelle"] == "plugin")

    check("ein fehlendes Plugin-Verzeichnis ist kein Fehler",
          backends.load_plugins("/gibt/es/nicht", verbose=False) == [])

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
