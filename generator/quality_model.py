"""Lernende Qualitätsbewertung aus den Urteilen des Anwenders.

Warum es das gibt: alle Schwellen im Quality Doctor sind an synthetischen
Testvideos festgelegt worden - saubere Sinusbewegungen, deren Wahrheit per
Konstruktion bekannt war. Echtes Material ist unregelmäßiger und liegt
systematisch niedriger. Statt weiter zu raten, lernt dieses Modul aus den
Kennzahlen echter Läufe zusammen mit dem menschlichen Urteil dazu.

Bewusst KEIN neuronales Netz: eine logistische Regression über sieben
Kennzahlen, in reinem numpy. Das reicht für die Aufgabe, braucht keine
zusätzliche Abhängigkeit von mehreren hundert Megabyte, und - wichtiger -
ihre Gewichte sind lesbar. Man kann nachsehen, WARUM das Modell so
entscheidet. Bei einem Netz ginge das nicht, und bei einer Bewertung, der
man vertrauen soll, ist das kein Nebenaspekt.

Die entscheidende Sicherung: das gelernte Modell wird nur übernommen, wenn
es in einer Kreuzvalidierung besser abschneidet als die bestehenden Regeln.
Ein Modell, das schlechter ist als das, was schon da war, wäre ein Rückschritt
mit dem Anschein von Fortschritt. Genau davor warnt auch die
Projektdokumentation (Abschnitt "Risiken": falsche Regel -> Before/After +
Reject).
"""

import datetime
import json
import os

import numpy as np


# Reihenfolge ist Teil des Dateiformats - beim Ändern MODEL_VERSION erhöhen,
# sonst werden alte Gewichte auf neue Kennzahlen angewendet.
FEATURES = (
    "concentration",
    "motion_range_fraction",
    "tracker_lost_fraction",
    "actions_per_minute",
    "gap_fraction",
    "speed_spikes",
    "action_count",
)

MODEL_VERSION = 1

# Unterhalb dieser Zahl beurteilter Läufe wird gar nicht erst gelernt. Aus
# fünf Beispielen ein Modell zu ziehen, das dann alle weiteren Bewertungen
# steuert, wäre schlimmer als die geratenen Schwellen: es sähe fundiert aus.
MIN_SAMPLES = 12
MIN_PER_CLASS = 4


def default_model_path():
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return os.path.join(local, "SamNPlayer", "qualitaetsmodell.json")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(xdg, "SamNPlayer", "qualitaetsmodell.json")


def _feature_vector(metrics):
    """Kennzahlen in einen Vektor. Fehlende Werte werden zu 0 - sie bedeuten
    "nicht gemessen", was für die betroffenen Kennzahlen der neutrale Fall
    ist (keine Lücken, keine Ausreißer)."""
    values = []
    for name in FEATURES:
        raw = metrics.get(name)
        value = 0.0 if raw is None else float(raw)
        if name == "actions_per_minute":
            value /= 100.0          # auf eine Größenordnung wie die anderen
        elif name == "action_count":
            value /= 1000.0
        elif name == "speed_spikes":
            value = min(value, 20.0) / 20.0
        values.append(value)
    return np.asarray(values, dtype=float)


def _sigmoid(z):
    # Über clip, weil exp(-z) bei großen negativen z überläuft.
    return 1.0 / (1.0 + np.exp(-np.clip(z, -30, 30)))


def _fit(X, y, epochs=4000, learning_rate=0.5, l2=0.01):
    """Logistische Regression per Gradientenabstieg.

    Die L2-Regularisierung ist hier nicht optional: bei einem Dutzend
    Beispielen und sieben Kennzahlen ließen sich die Daten sonst exakt
    auswendig lernen, und das Modell wäre auf allem Neuen wertlos.
    """
    n_features = X.shape[1]
    weights = np.zeros(n_features)
    bias = 0.0
    for _ in range(epochs):
        predictions = _sigmoid(X @ weights + bias)
        error = predictions - y
        weights -= learning_rate * (X.T @ error / len(y) + l2 * weights)
        bias -= learning_rate * error.mean()
    return weights, bias


def _predict(weights, bias, X):
    return _sigmoid(X @ np.asarray(weights) + bias)


def _leave_one_out_accuracy(X, y):
    """Kreuzvalidierung: jeder Lauf wird einmal vorhergesagt, ohne dass er
    beim Lernen dabei war. Bei so wenigen Beispielen ist das die einzige
    ehrliche Art zu messen - auf den eigenen Trainingsdaten sieht jedes
    Modell gut aus."""
    correct = 0
    for i in range(len(y)):
        keep = np.arange(len(y)) != i
        if len(np.unique(y[keep])) < 2:
            continue          # ohne beide Klassen ist nichts zu lernen
        weights, bias = _fit(X[keep], y[keep])
        predicted = _predict(weights, bias, X[i:i + 1])[0] >= 0.5
        correct += int(bool(predicted) == bool(y[i]))
    return correct / len(y)


def collect_training_data(records):
    """Zieht beurteilte Läufe aus dem Messbericht.

    "grenzwertig" wird als NICHT bestanden gewertet: ein Skript, bei dem der
    Anwender zögert, soll gemeldet werden. Lieber eine Rückfrage zu viel als
    ein stillschweigend durchgewinktes schlechtes Ergebnis.
    """
    X, y, sources = [], [], []
    for record in records:
        feedback = record.get("feedback")
        if not feedback:
            continue
        metrics = (record.get("quality") or {}).get("metrics") or {}
        if not metrics:
            continue
        X.append(_feature_vector(metrics))
        y.append(1.0 if feedback.get("verdict") == "brauchbar" else 0.0)
        sources.append(record.get("video_name", "?"))
    if not X:
        return np.zeros((0, len(FEATURES))), np.zeros(0), []
    return np.vstack(X), np.asarray(y), sources


def rule_based_accuracy(records):
    """Wie gut trifft die BESTEHENDE Regelbewertung die Urteile? Das ist die
    Messlatte, die ein gelerntes Modell schlagen muss."""
    total = correct = 0
    for record in records:
        feedback = record.get("feedback")
        if not feedback:
            continue
        quality = record.get("quality") or {}
        if "passed" not in quality:
            continue
        total += 1
        expected = feedback.get("verdict") == "brauchbar"
        correct += int(bool(quality["passed"]) == expected)
    return (correct / total) if total else 0.0


def train(records, model_path=None):
    """Lernt ein Modell und speichert es, wenn es die Regeln schlägt.

    Gibt (erfolg, bericht) zurück. Der Bericht ist für Menschen gedacht -
    er soll erklären, was passiert ist, nicht nur eine Zahl liefern.
    """
    X, y, _ = collect_training_data(records)
    lines = [f"Beurteilte Läufe: {len(y)}"]

    if len(y) < MIN_SAMPLES:
        lines.append(f"Zu wenige für ein Modell (mindestens {MIN_SAMPLES}). "
                     "Weiter Urteile sammeln - die Regelbewertung bleibt aktiv.")
        return False, "\n".join(lines)

    positives, negatives = int(y.sum()), int(len(y) - y.sum())
    lines.append(f"davon brauchbar: {positives}, nicht brauchbar: {negatives}")
    if positives < MIN_PER_CLASS or negatives < MIN_PER_CLASS:
        lines.append(f"Von jeder Sorte werden mindestens {MIN_PER_CLASS} gebraucht. "
                     "Aus einseitigen Daten lässt sich nichts Sinnvolles lernen.")
        return False, "\n".join(lines)

    # Standardisieren, damit Kennzahlen mit großem Wertebereich das Modell
    # nicht allein dominieren.
    mean = X.mean(axis=0)
    scale = X.std(axis=0)
    scale[scale < 1e-6] = 1.0
    Xn = (X - mean) / scale

    learned_accuracy = _leave_one_out_accuracy(Xn, y)
    baseline = rule_based_accuracy(records)
    lines.append(f"Trefferquote gelernt (kreuzvalidiert): {learned_accuracy*100:.0f}%")
    lines.append(f"Trefferquote der bisherigen Regeln:     {baseline*100:.0f}%")

    if learned_accuracy <= baseline:
        lines.append("Das gelernte Modell ist nicht besser als die Regeln und wird "
                     "NICHT übernommen. Ein Modell, das schlechter ist als das, was "
                     "schon da war, wäre ein Rückschritt mit dem Anschein von "
                     "Fortschritt.")
        return False, "\n".join(lines)

    weights, bias = _fit(Xn, y)
    model = {
        "version": MODEL_VERSION,
        "features": list(FEATURES),
        "weights": [float(w) for w in weights],
        "bias": float(bias),
        "mean": [float(v) for v in mean],
        "scale": [float(v) for v in scale],
        "samples": int(len(y)),
        "accuracy": round(float(learned_accuracy), 3),
        "baseline_accuracy": round(float(baseline), 3),
        "trained_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    }
    path = model_path or default_model_path()
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(model, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, path)

    lines.append(f"Modell übernommen und gespeichert: {path}")
    lines.append("")
    lines.append("Einfluss der Kennzahlen (positiv = spricht für 'brauchbar'):")
    for name, weight in sorted(zip(FEATURES, weights), key=lambda p: -abs(p[1])):
        lines.append(f"  {name:24s} {weight:+.2f}")
    return True, "\n".join(lines)


def load(model_path=None):
    """Lädt ein gespeichertes Modell, oder None."""
    path = model_path or default_model_path()
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8") as fh:
            model = json.load(fh)
    except (OSError, json.JSONDecodeError):
        return None
    # Ein Modell aus einer anderen Kennzahlreihenfolge wäre stillschweigend
    # falsch - lieber verwerfen.
    if model.get("version") != MODEL_VERSION or model.get("features") != list(FEATURES):
        return None
    return model


def score(model, metrics):
    """Wahrscheinlichkeit, dass der Anwender das Ergebnis brauchbar findet."""
    if not model:
        return None
    x = _feature_vector(metrics)
    mean = np.asarray(model["mean"])
    scale = np.asarray(model["scale"])
    xn = (x - mean) / np.where(scale < 1e-6, 1.0, scale)
    return float(_predict(model["weights"], model["bias"], xn.reshape(1, -1))[0])


def describe(model):
    if not model:
        return "Kein gelerntes Modell vorhanden - es gilt die Regelbewertung."
    lines = [
        f"Gelernt aus {model['samples']} beurteilten Läufen am {model['trained_at'][:10]}",
        f"Trefferquote kreuzvalidiert {model['accuracy']*100:.0f}% "
        f"(Regeln allein: {model['baseline_accuracy']*100:.0f}%)",
        "",
        "Einfluss der Kennzahlen (positiv = spricht für 'brauchbar'):",
    ]
    for name, weight in sorted(zip(model["features"], model["weights"]),
                               key=lambda p: -abs(p[1])):
        lines.append(f"  {name:24s} {weight:+.2f}")
    return "\n".join(lines)
