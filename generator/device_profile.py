"""Geräteverträglichkeit eines Funscripts.

Die Kennzahlen und Grenzwerte hier sind nicht selbst ausgedacht, sondern
aus den in der Funscript-Gemeinschaft etablierten Werkzeugen übernommen -
damit erzeugte Skripte auch auf fremden Geräten und in fremden Playern
brauchbar sind, nicht nur im eigenen Player.

Quellen der Zahlen:

* **Mindestabstand 100 ms.** Launchcontrol (funjack) verwendet beim Senden
  an das Gerät eine Schwelle von 100 ms; Skripte, die schneller feuern,
  geraten in diesen Abschnitten aus dem Takt. Actions dichter als das sind
  also nicht nur überflüssig, sie schaden.

* **Langsamster sinnvoller Hub ~900 ms.** Aus der Scripting-Anleitung von
  funjack: langsamere Vollhübe kann das Gerät nicht ausführen, es fährt
  schneller und steht dann still - im Skript entsteht eine Pause, die so
  nicht gemeint war.

* **Intensität = 500 × |Δpos| / |Δt|.** Definition aus funscript-utils:
  Positionsbewegung pro halbe Sekunde, wobei 100 einem vollen Hub pro
  Sekunde entspricht. Dieselbe Größe nutzen OpenFunscripter, Funscript.io
  und XBVR, um Skripte nach Tempo zu sortieren - sie gehört deshalb in
  jeden Bericht.

* **Nutzbarer Positionsbereich.** Launchcontrol begrenzt auf 5-95; die
  äußersten Ränder sind auf realer Hardware nicht zuverlässig erreichbar.

Wichtig zur Einordnung: Der Sam Neo 2 ist kein linearer Stroker, sondern
steuert Vibration und Sog. Diese Grenzwerte gelten für ihn nicht direkt.
Sie gelten für die erzeugten DATEIEN, die auf anderen Geräten abgespielt
werden - und genau deshalb sind sie hier relevant.
"""

import numpy as np


# Mindestabstand zwischen zwei Actions in Millisekunden (Launchcontrol).
MIN_INTERVAL_MS = 100.0

# Langsamster sinnvoller Vollhub in Millisekunden.
SLOWEST_FULL_STROKE_MS = 900.0

# Zuverlässig erreichbarer Positionsbereich.
USABLE_POSITION_MIN = 5
USABLE_POSITION_MAX = 95


def intensity(actions):
    """Intensität je Intervall nach der Definition aus funscript-utils:
    500 * |Δpos| / |Δt|. Ein Wert von 100 entspricht einem vollen Hub pro
    Sekunde."""
    if len(actions) < 2:
        return np.zeros(0)
    at = np.array([a["at"] for a in actions], dtype=float)
    pos = np.array([a["pos"] for a in actions], dtype=float)
    dt = np.diff(at)
    dpos = np.abs(np.diff(pos))
    valid = dt > 0
    result = np.zeros(len(dt))
    result[valid] = 500.0 * dpos[valid] / dt[valid]
    return result


def evaluate(actions):
    """Prüft ein Skript auf Geräteverträglichkeit.

    Gibt (metrics, warnings) zurück. Die Warnungen sind bewusst so
    formuliert, dass klar wird, WAS auf dem Gerät passiert - nicht nur, dass
    ein Grenzwert verletzt wurde.
    """
    metrics = {}
    warnings = []
    if len(actions) < 2:
        return metrics, warnings

    at = np.array([a["at"] for a in actions], dtype=float)
    pos = np.array([a["pos"] for a in actions], dtype=float)
    dt = np.diff(at)
    dpos = np.abs(np.diff(pos))

    # --- Intensität als Tempo-Kennzahl ---------------------------------
    values = intensity(actions)
    if len(values):
        # Gewichtet nach Dauer: ein kurzes schnelles Intervall soll den
        # Durchschnitt nicht so stark heben wie ein langes.
        weights = np.where(dt > 0, dt, 0)
        metrics["avg_intensity"] = (round(float(np.average(values, weights=weights)), 1)
                                    if weights.sum() > 0 else 0.0)
        metrics["peak_intensity"] = round(float(np.percentile(values, 95)), 1)

    # --- Zu dicht aufeinanderfolgende Actions --------------------------
    too_close = int(np.sum((dt > 0) & (dt < MIN_INTERVAL_MS)))
    metrics["actions_too_close"] = too_close
    if too_close:
        share = too_close / len(dt)
        metrics["too_close_share"] = round(share, 3)
        if share > 0.05:
            warnings.append(
                f"{too_close} Actions ({share*100:.0f}%) folgen schneller als "
                f"{MIN_INTERVAL_MS:.0f}ms aufeinander - solche Abschnitte geraten auf "
                "dem Gerät aus dem Takt, weil es sie nicht mehr einzeln ausführen kann")

    # --- Zu langsame Vollhübe ------------------------------------------
    # Nur große Bewegungen betrachten: bei einer kleinen Positionsänderung
    # ist eine lange Dauer normal und richtig.
    large_moves = dpos >= 50
    slow_full = int(np.sum(large_moves & (dt > SLOWEST_FULL_STROKE_MS)))
    metrics["slow_full_strokes"] = slow_full
    if slow_full and large_moves.sum() > 0:
        share = slow_full / int(large_moves.sum())
        if share > 0.25:
            warnings.append(
                f"{slow_full} große Bewegungen dauern länger als "
                f"{SLOWEST_FULL_STROKE_MS:.0f}ms - das Gerät fährt sie schneller und "
                "steht dann still, im Ergebnis entstehen ungewollte Pausen")

    # --- Genutzter Positionsbereich ------------------------------------
    used_low, used_high = float(pos.min()), float(pos.max())
    metrics["position_span"] = round(used_high - used_low, 1)
    if used_high - used_low < 40:
        warnings.append(
            f"Das Skript nutzt nur den Bereich {used_low:.0f}-{used_high:.0f} von 100 - "
            "auf dem Gerät bleibt die Bewegung entsprechend schwach")

    # Actions außerhalb des zuverlässig erreichbaren Bereichs sind kein
    # Fehler, aber sie gehen auf realer Hardware verloren.
    outside = int(np.sum((pos < USABLE_POSITION_MIN) | (pos > USABLE_POSITION_MAX)))
    metrics["actions_outside_usable_range"] = outside

    return metrics, warnings
