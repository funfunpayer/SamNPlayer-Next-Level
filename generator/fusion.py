"""Zusammenführung mehrerer Messquellen über Zeitstempel.

Umsetzung der Abschnitte 10 und 11 des Zielkonzepts.

Zwei Regeln bestimmen den Entwurf:

**Abgleich über Zeitstempel, nicht über Index.** Quellen können mit
unterschiedlichen Raten messen - der CSRT-Weg liefert einen Wert pro
Videoframe, das Flow-Backend ebenso, aber nach einem Szenenschnitt oder bei
verworfenen Frames laufen die Zählungen auseinander. Index gegen Index zu
addieren verschiebt dann stillschweigend die Zeitachse. Deshalb wird auf ein
gemeinsames Zeitraster interpoliert.

**Confidence muss aufgeschlüsselt sein.** Ein einzelner Wert "der
Algorithmus meint, es sei gut" lässt sich nicht überprüfen. Hier setzt sie
sich aus drei messbaren Anteilen zusammen, die getrennt zurückgegeben
werden - damit später nachvollziehbar bleibt, WARUM einem Sample geglaubt
wurde.

GEMESSENER STAND - BEWUSST NICHT IM ERZEUGUNGSPFAD VERDRAHTET.

Mit den beiden heute vorhandenen Quellen (CSRT-Tracker und Flow-Backend)
bringt die Fusion nichts. Gegen bekannte Wahrheit gemessen, Korrelation:

    Video          CSRT     Flow     fusioniert   Orakel
    clean_1hz      1.000    0.926    0.985        1.000
    occluded       0.293    0.747    0.620        0.858

Die Fusion landet jedes Mal ZWISCHEN den Quellen und schlägt nie die beste.
Der Grund ist strukturell und nicht durch bessere Gewichte behebbar: bei nur
zwei Quellen ist die gegenseitige Übereinstimmung symmetrisch. Weichen beide
voneinander ab, sagt das Maß, DASS sie uneinig sind, aber nicht, WER recht
hat. Nur die quellenweite Grundgüte kann das unterscheiden, und die ist ein
grober Skalar für das ganze Video.

Die Orakel-Zeile ist die Obergrenze für jede denkbare Gewichtung dieser
beiden Quellen - pro Sample die jeweils bessere gewählt. Bei clean_1hz liegt
sie exakt auf der besten Einzelquelle: dort ist schlicht nichts zu gewinnen.
Bei occluded liegen zwischen bester Einzelquelle (0.747) und Obergrenze
(0.858) elf Punkte, die eine perfekte Auswahl holen könnte - eine, die pro
Sample weiß, welche Quelle gerade danebenliegt. Genau das kann keine der
drei Confidence-Komponenten leisten.

Sinnvoll wird das Modul erst mit einer DRITTEN unabhängigen Quelle: dann ist
der Median über die Quellen ein tragfähiger Bezugspunkt, und die
Übereinstimmung sagt tatsächlich, wer ausreißt. Bis dahin bleibt es
ungenutzt im Baum - mit Tests, damit es beim Hinzufügen der dritten Quelle
sofort einsatzbereit ist.

Wichtig zur Einordnung: die Quellen liefern Positionen in verschiedenen
Bezugssystemen (Kastenmitte gegen Bewegungsschwerpunkt). Sie werden deshalb
vor dem Vergleich einzeln auf 0-100 normalisiert. Absolute Amplituden sind
danach nicht mehr vergleichbar - die Form ist es, und auf die kommt es nach
der Normalisierung ohnehin an.
"""

import numpy as np


def _normalize(values, percentile=2.0):
    """Auf 0-100, robust gegen einzelne Ausreißer."""
    values = np.asarray(values, dtype=float)
    if len(values) == 0:
        return values
    lo = float(np.percentile(values, percentile))
    hi = float(np.percentile(values, 100.0 - percentile))
    if hi - lo < 1e-9:
        lo, hi = float(values.min()), float(values.max())
    if hi - lo < 1e-9:
        return np.full_like(values, 50.0)
    return np.clip((values - lo) / (hi - lo), 0.0, 1.0) * 100.0


def align(sources, step_ms=None):
    """Bringt alle Quellen auf ein gemeinsames Zeitraster.

    sources: Liste von (name, timestamps_ms, positions).
    Gibt (grid, {name: werte}) zurück.

    Das Raster deckt nur den Zeitbereich ab, den ALLE Quellen abdecken.
    Außerhalb müsste extrapoliert werden, und eine extrapolierte Messung ist
    keine Messung.
    """
    if not sources:
        return np.zeros(0), {}

    starts = [float(np.min(ts)) for _, ts, _ in sources]
    ends = [float(np.max(ts)) for _, ts, _ in sources]
    start, end = max(starts), min(ends)
    if end <= start:
        return np.zeros(0), {}

    if step_ms is None:
        # Feinste vorhandene Auflösung, damit keine Quelle künstlich
        # geglättet wird.
        steps = []
        for _, ts, _ in sources:
            diffs = np.diff(np.asarray(ts, dtype=float))
            diffs = diffs[diffs > 0]
            if len(diffs):
                steps.append(float(np.median(diffs)))
        step_ms = min(steps) if steps else 40.0

    grid = np.arange(start, end, max(step_ms, 1.0))
    aligned = {}
    for name, ts, pos in sources:
        ts = np.asarray(ts, dtype=float)
        pos = _normalize(pos)
        order = np.argsort(ts)
        aligned[name] = np.interp(grid, ts[order], pos[order])
    return grid, aligned


def _temporal_consistency(values, window=5):
    """Wie gut passt jeder Wert zu seiner unmittelbaren Umgebung?

    Ein Sprung, der im nächsten Sample wieder verschwindet, ist ein
    Messfehler und keine Bewegung. Gemessen als Abweichung vom gleitenden
    Median, auf 0-1 abgebildet.
    """
    values = np.asarray(values, dtype=float)
    if len(values) < window * 2:
        return np.ones(len(values))
    padded = np.pad(values, window, mode="edge")
    smooth = np.array([np.median(padded[i:i + 2 * window + 1]) for i in range(len(values))])
    deviation = np.abs(values - smooth)
    scale = np.percentile(deviation, 90) + 1e-6
    return np.clip(1.0 - deviation / (3.0 * scale), 0.0, 1.0)


def _cross_source_agreement(aligned):
    """Wie sehr stimmt jede Quelle in jedem Moment mit den anderen überein?

    Das ist der wertvollste Anteil der Confidence: eine einzelne Quelle kann
    nicht wissen, dass sie danebenliegt - im Vergleich mit anderen wird es
    sichtbar. Bezugspunkt ist der Median aller Quellen, nicht der Mittelwert,
    damit eine ausgerissene Quelle den Bezugspunkt nicht selbst verschiebt.
    """
    names = list(aligned)
    if len(names) < 2:
        return {name: np.ones(len(aligned[name])) for name in names}

    stacked = np.vstack([aligned[name] for name in names])
    reference = np.median(stacked, axis=0)
    agreement = {}
    for i, name in enumerate(names):
        deviation = np.abs(stacked[i] - reference)
        # 25 Punkte Abweichung auf der 0-100-Skala gelten als vollständige
        # Uneinigkeit. Darüber trägt die Quelle in diesem Moment nichts bei.
        agreement[name] = np.clip(1.0 - deviation / 25.0, 0.0, 1.0)
    return agreement


def fuse(sources, base_confidence=None, step_ms=None):
    """Führt mehrere Messquellen zu einem Signal zusammen.

    base_confidence: optional {name: 0-1}, eine quellenweite Grundgüte -
    etwa aus der Verlustquote des Trackers oder der Uneinigkeit der
    Zentrumsschätzer.

    Rückgabe: dict mit Zeitraster, fusioniertem Signal, Gesamt-Confidence
    und den EINZELNEN Confidence-Anteilen je Quelle. Letztere sind der
    eigentliche Zweck der Aufschlüsselung: ohne sie ließe sich nicht
    beantworten, warum die Engine einem Sample geglaubt hat.
    """
    grid, aligned = align(sources, step_ms=step_ms)
    if len(grid) == 0:
        raise ValueError("Quellen haben keinen gemeinsamen Zeitbereich")

    base_confidence = base_confidence or {}
    temporal = {name: _temporal_consistency(values) for name, values in aligned.items()}
    agreement = _cross_source_agreement(aligned)

    weights = {}
    for name in aligned:
        base = float(base_confidence.get(name, 1.0))
        # Multiplikativ: jeder Anteil kann für sich allein eine Quelle
        # entwerten. Ein Mittelwert würde eine Quelle, die den anderen klar
        # widerspricht, noch mitreden lassen, solange sie nur glatt ist.
        weights[name] = np.clip(base * temporal[name] * agreement[name], 1e-6, None)

    stacked = np.vstack([aligned[name] for name in aligned])
    weight_stack = np.vstack([weights[name] for name in aligned])
    fused = (stacked * weight_stack).sum(axis=0) / weight_stack.sum(axis=0)
    confidence = np.clip(weight_stack.sum(axis=0) / len(aligned), 0.0, 1.0)

    return {
        "at": grid,
        "pos": fused,
        "confidence": confidence,
        "sources": aligned,
        "components": {
            name: {
                "base": float(base_confidence.get(name, 1.0)),
                "temporal_consistency": temporal[name],
                "cross_source_agreement": agreement[name],
                "weight": weights[name],
            }
            for name in aligned
        },
    }


def describe(result):
    """Kurzbericht, welche Quelle wie viel beigetragen hat."""
    lines = []
    total = sum(float(np.mean(c["weight"])) for c in result["components"].values())
    for name, comp in result["components"].items():
        share = float(np.mean(comp["weight"])) / total if total > 0 else 0.0
        lines.append(
            f"  {name:12s} Anteil {share*100:5.1f}%  "
            f"zeitlich {np.mean(comp['temporal_consistency']):.2f}  "
            f"Übereinstimmung {np.mean(comp['cross_source_agreement']):.2f}")
    lines.append(f"  mittlere Confidence: {float(np.mean(result['confidence'])):.2f}")
    return "\n".join(lines)
