"""Bewegungssignatur einer Szene - Wiedererkennung ohne Modell.

Abgrenzung vorweg, weil sie über den ganzen Ansatz entscheidet: Dieses Modul
erkennt NICHT, *was* zu sehen ist. Eine Stellung als solche zu benennen wäre
Bedeutungserkennung, und dafür braucht es ein trainiertes Modell und
beschriftete Daten.

Was es kann, ist etwas anderes und für unseren Zweck oft ausreichend:
*wiedererkennen, dass eine Szene derselben Art ist wie eine früher schon
gesehene*. Verschiedene Aufnahmesituationen erzeugen charakteristisch
verschiedene Bewegungsmuster - Hauptrichtung, Anzahl und Anordnung bewegter
Regionen, Hubform, Rhythmus, Kamerastabilität. Diese Größen sind messbar.

Benennt der Anwender eine Szene einmal, lässt sich die Benennung über die
Signatur auf ähnliche Szenen übertragen - und mit ihr die Parameter, die dort
nachweislich funktioniert haben. Das ist fallbasiertes Wiedererkennen, kein
Lernen von Bedeutung.

Die Signatur ist bewusst kurz und aus Größen zusammengesetzt, die einzeln
erklärbar sind. Eine hundertdimensionale Merkmalsliste würde vielleicht
besser trennen, aber niemand könnte mehr nachvollziehen, warum zwei Szenen
als ähnlich galten - und genau das ist bei einer Zuordnung, der man Parameter
anvertraut, der entscheidende Punkt.
"""

import numpy as np

import cv2


# Reihenfolge ist Teil des Formats - beim Ändern SIGNATURE_VERSION erhöhen.
SIGNATURE_FIELDS = (
    "vertical_share",        # Anteil senkrechter an der Gesamtbewegung
    "motion_center_y",       # Lage des Bewegungsschwerpunkts, 0=oben 1=unten
    "motion_center_x",
    "motion_spread",         # Wie weit verteilt sich die Bewegung im Bild
    "region_count",          # Zahl getrennter Bewegungsregionen
    "camera_motion",         # Wie unruhig ist die Kamera
    "stroke_symmetry",       # Auf- und Abbewegung gleich schnell?
    "rhythm_strength",       # spektrale Konzentration des Hauptsignals
)

SIGNATURE_VERSION = 1


def _normalize01(value, scale):
    return float(np.clip(value / scale, 0.0, 1.0)) if scale > 0 else 0.0


def extract(video_path, max_seconds=30, sample_every=2, grid=(8, 6)):
    """Berechnet die Signatur eines Videos oder Abschnitts."""
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0

    ok, first = cap.read()
    if not ok:
        cap.release()
        raise RuntimeError("Video enthält keine lesbaren Frames")

    height, width = first.shape[:2]
    scale = 240.0 / width if width > 240 else 1.0
    small_w, small_h = int(width * scale), int(height * scale)
    prev = cv2.cvtColor(cv2.resize(first, (small_w, small_h)), cv2.COLOR_BGR2GRAY)

    cols, rows = grid
    cell_w, cell_h = small_w / cols, small_h / rows

    vertical, horizontal = 0.0, 0.0
    centers_y, centers_x = [], []
    camera_steps = []
    cell_energy = np.zeros((rows, cols))
    signal = []

    max_frames = int(max_seconds * fps)
    idx = 0
    while idx < max_frames:
        for _ in range(sample_every):
            ok, frame = cap.read()
            idx += 1
            if not ok:
                break
        if not ok:
            break
        gray = cv2.cvtColor(cv2.resize(frame, (small_w, small_h)), cv2.COLOR_BGR2GRAY)
        flow = cv2.calcOpticalFlowFarneback(prev, gray, None, 0.5, 3, 15, 3, 5, 1.2, 0)

        # Kamerabewegung als robuster Median - der bewegte Bildteil ist in
        # aller Regel die Minderheit der Pixel.
        cam_x = float(np.median(flow[..., 0]))
        cam_y = float(np.median(flow[..., 1]))
        camera_steps.append(abs(cam_x) + abs(cam_y))
        flow = flow - np.array([cam_x, cam_y], dtype=np.float32)

        magnitude = np.linalg.norm(flow, axis=2)
        vertical += float(np.abs(flow[..., 1]).sum())
        horizontal += float(np.abs(flow[..., 0]).sum())

        total = magnitude.sum()
        if total > 1e-6:
            ys = np.arange(small_h, dtype=np.float32)[:, None]
            xs = np.arange(small_w, dtype=np.float32)[None, :]
            centers_y.append(float((magnitude * ys).sum() / total) / small_h)
            centers_x.append(float((magnitude * xs).sum() / total) / small_w)
            signal.append(centers_y[-1])

            for r in range(rows):
                for c in range(cols):
                    y0, y1 = int(r * cell_h), int((r + 1) * cell_h)
                    x0, x1 = int(c * cell_w), int((c + 1) * cell_w)
                    cell_energy[r][c] += float(magnitude[y0:y1, x0:x1].sum())

        prev = gray
    cap.release()

    if not centers_y:
        raise RuntimeError("Keine verwertbare Bewegung gefunden")

    total_directional = vertical + horizontal
    signature = {
        "vertical_share": (vertical / total_directional) if total_directional > 0 else 0.5,
        "motion_center_y": float(np.median(centers_y)),
        "motion_center_x": float(np.median(centers_x)),
        # Streuung des Schwerpunkts: klein bei einer klar umgrenzten Region,
        # groß wenn sich die Bewegung über das Bild verteilt.
        "motion_spread": _normalize01(float(np.std(centers_y) + np.std(centers_x)), 0.3),
        "region_count": _count_regions(cell_energy),
        "camera_motion": _normalize01(float(np.median(camera_steps)), 2.0),
        "stroke_symmetry": _stroke_symmetry(np.asarray(signal)),
        "rhythm_strength": _rhythm_strength(np.asarray(signal)),
    }
    return signature


def _count_regions(cell_energy):
    """Zahl deutlich getrennter Bewegungsregionen, auf 0-1 abgebildet.

    Eine Szene mit zwei getrennt bewegten Bereichen ist etwas grundsätzlich
    anderes als eine mit einem - auch wenn Richtung und Rhythmus gleich sind.
    """
    if cell_energy.max() <= 0:
        return 0.0
    mask = (cell_energy >= cell_energy.max() * 0.4).astype(np.uint8)
    count, _ = cv2.connectedComponents(mask)
    # connectedComponents zählt den Hintergrund mit.
    return _normalize01(float(max(0, count - 1)), 4.0)


def _stroke_symmetry(signal):
    """Sind Auf- und Abbewegung gleich schnell? 0.5 = symmetrisch.

    Ein schneller Anstieg mit langsamem Absinken ist ein anderes
    Bewegungsmuster als ein gleichmäßiges Hin und Her, auch bei gleicher
    Frequenz und Amplitude.
    """
    if len(signal) < 8:
        return 0.5
    steps = np.diff(signal)
    rising = float(np.sum(steps > 0))
    falling = float(np.sum(steps < 0))
    if rising + falling < 1:
        return 0.5
    return rising / (rising + falling)


def _rhythm_strength(signal):
    if len(signal) < 16:
        return 0.0
    values = np.asarray(signal, dtype=float)
    x = np.arange(len(values))
    values = values - np.polyval(np.polyfit(x, values, 1), x)
    spectrum = np.abs(np.fft.rfft(values))[1:]
    if spectrum.sum() <= 0 or len(spectrum) <= 3:
        return 0.0
    return float(np.sort(spectrum)[-3:].sum() / spectrum.sum())


def distance(a, b):
    """Abstand zweier Signaturen, 0 = identisch.

    Gleichgewichtet über alle Felder, weil kein Feld erwiesenermaßen
    wichtiger ist als die anderen. Sobald genug benannte Beispiele vorliegen,
    ließen sich Gewichte lernen - vorher wäre jede Gewichtung geraten.
    """
    total = 0.0
    for field in SIGNATURE_FIELDS:
        total += (float(a.get(field, 0.0)) - float(b.get(field, 0.0))) ** 2
    return float(np.sqrt(total / len(SIGNATURE_FIELDS)))


def default_labels_path():
    """Ablageort analog zu backends.default_plugin_dir() /
    quality_model.default_model_path() - ein Nutzerkonfigurationsordner,
    kein Pfad im Repository/Binary."""
    import os

    local = os.environ.get("LOCALAPPDATA")
    if local:
        return os.path.join(local, "SamNPlayer", "szenensignaturen.jsonl")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(xdg, "SamNPlayer", "szenensignaturen.jsonl")


def save_labelled(path, label, signature, parameters=None):
    """Merkt sich eine benannte Szene samt der Parameter, die dort
    funktioniert haben.

    Format wie beim Messbericht: eine JSON-Zeile je Eintrag, anhängend
    geschrieben. Das übersteht Abbrüche und lässt sich von Hand ansehen und
    korrigieren - bei Angaben, die der Anwender selbst gemacht hat, ist das
    wichtiger als Kompaktheit.
    """
    import json
    import os

    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    record = {
        "version": SIGNATURE_VERSION,
        "label": label,
        "signature": signature,
        "parameters": parameters or {},
    }
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, ensure_ascii=False) + "\n")


def load_labelled(path):
    """Liest die benannten Szenen. Kaputte Zeilen werden übersprungen."""
    import json
    import os

    entries = []
    if not os.path.exists(path):
        return entries
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            # Einträge aus einer anderen Signaturfassung wären
            # stillschweigend falsch zugeordnet - lieber verwerfen.
            if record.get("version") == SIGNATURE_VERSION:
                entries.append(record)
    return entries


def find_similar(signature, known, max_distance=0.15):
    """Sucht die ähnlichste benannte Szene.

    known: Liste von {"label": str, "signature": dict, ...}

    Gibt (eintrag, abstand) oder (None, abstand) zurück. Oberhalb von
    max_distance wird NICHTS zugeordnet: eine falsche Zuordnung überträgt
    Parameter, die nicht passen, und ist damit schlechter als gar keine.
    """
    best, best_distance = None, float("inf")
    for entry in known:
        d = distance(signature, entry.get("signature", {}))
        if d < best_distance:
            best, best_distance = entry, d
    if best is None or best_distance > max_distance:
        return None, best_distance
    return best, best_distance
