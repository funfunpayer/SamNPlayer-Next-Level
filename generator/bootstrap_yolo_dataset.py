#!/usr/bin/env python3
"""bootstrap_yolo_dataset.py - erzeugt einen YOLO-Trainingsdatensatz aus
bereits vorhandenem klassischen Tracking (CSRT, track_roi), statt Boxen von
Hand zu zeichnen.

Hintergrund (docs/AI_ADAPTER.md, "Still open: no bundled/recommended ONNX
model"): `generator/ai_roi.py` erwartet ein ONNX-Modell mit dem Vertrag
"(N,6) normalisierte [x0,y0,x1,y1,confidence,class_id]", aber es existiert
keins. Ein Objekterkenner von Grund auf zu trainieren braucht annotierte
Frames - CSRT liefert genau das schon: eine Box pro Frame, für jeden Clip,
den wir bereits erfolgreich getrackt haben. Der Nutzer wollte beides
kombinieren (Chat, 15. September 2026): dieses Skript liefert die
Bootstrap-Basis aus dem Tracking, danach lässt sich der Datensatz mit einem
Standard-Werkzeug (LabelImg, CVAT, makesense.ai - alle können YOLO-Format
lesen/schreiben) von Hand nachbessern, bevor trainiert wird.

WICHTIG - was dieses Modell NICHT genauer machen kann als das Original: der
Detektor lernt, die CSRT-Box nachzuahmen. Er wird nie zuverlässiger als der
Tracker, der die Trainingsdaten geliefert hat - sein Nutzen ist Geschwindigkeit
(ein Einzelbild-Vorschlag statt iterativem Tracking über viele Frames) und
Generalisierung auf neue Clips ohne erneutes Tracking, nicht höhere Präzision
auf demselben Material. Von Hand nachgebesserte Beispiele (falsche/verlorene
CSRT-Boxen korrigiert) sind deshalb wichtig, nicht nur Fleißarbeit.

Format (Ultralytics-YOLO-Standard, siehe
https://docs.ultralytics.com/datasets/detect/):
  <output_dir>/
    images/train/*.jpg, images/val/*.jpg
    labels/train/*.txt, labels/val/*.txt   (eine Zeile "class xc yc w h",
                                             alle Werte 0..1 relativ zur
                                             Bildgröße)
    data.yaml

Wiederholte Aufrufe auf denselben output_dir mit verschiedenen Videos/ROIs
HÄNGEN AN (splittet neu in train/val, überschreibt bestehende Bilder nicht -
Dateinamen enthalten den Video-Stem), damit sich der Datensatz über mehrere
Clips aufbauen lässt, wie vom Nutzer gewünscht.

Mehrere Inhaltskategorien im selben Datensatz (Chat, 15. September 2026 -
"komplett kombinieren was zusammen passt", mit besonderem Augenmerk auf
TF/TJ-Mischformen): --class-name ordnet jedem Aufruf eine Kategorie zu
(z.B. "blowjob", "tf_tj_mix"); die Zuordnung Name->ID wird in
<output_dir>/classes.json über mehrere Aufrufe hinweg gemerkt, damit
data.yaml am Ende ALLE bisher angelegten Kategorien enthält statt nur die
des letzten Aufrufs. Eine eigene Klasse für TF/TJ-Inhalte bedeutet: der
Detektor lernt diese Bewegungsform getrennt von reinem Blowjob-Material zu
erkennen, statt sie in einer gemeinsamen Klasse zu verwässern - wie viele
Beispiele jede Kategorie bekommt (und damit wie stark sie im Ergebnis
gewichtet ist), bestimmt allein, wie oft mit dem jeweiligen --class-name
aufgerufen wird.

Nutzung:
  python3 bootstrap_yolo_dataset.py --video clip.mp4 --roi 61,63,154,72 \
      --output-dir ./yolo_dataset --class-name tf_tj_mix
"""

import argparse
import hashlib
import json
import os
import random
import shutil
import sys
from pathlib import Path

_IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".webp")


def _list_split_images(img_dir):
    """Sorted image filenames in a YOLO split folder (train/val)."""
    if not os.path.isdir(img_dir):
        return []
    names = []
    for name in sorted(os.listdir(img_dir)):
        if name.lower().endswith(_IMAGE_EXTS):
            names.append(name)
    return names


def ensure_nonempty_splits(output_dir):
    """Guarantee images/train and images/val each have ≥1 image when the
    other side has samples.

    Ultralytics dies with AssertionError/FileNotFoundError when data.yaml
    points at an empty images/val (common with tiny datasets, unlucky
    val_fraction draws, or after the review UI discards every val sample).
    Copies image+matching label from the non-empty split into the empty
    one (same stem — fine for small datasets; avoids rewriting data.yaml).

    Returns (train_count, val_count) after healing.
    """
    img_train = os.path.join(output_dir, "images", "train")
    img_val = os.path.join(output_dir, "images", "val")
    lbl_train = os.path.join(output_dir, "labels", "train")
    lbl_val = os.path.join(output_dir, "labels", "val")
    for d in (img_train, img_val, lbl_train, lbl_val):
        os.makedirs(d, exist_ok=True)

    train_imgs = _list_split_images(img_train)
    val_imgs = _list_split_images(img_val)

    def _copy_one(src_img_dir, src_lbl_dir, dst_img_dir, dst_lbl_dir, name):
        src_img = os.path.join(src_img_dir, name)
        dst_img = os.path.join(dst_img_dir, name)
        if not os.path.exists(dst_img):
            shutil.copy2(src_img, dst_img)
        stem, _ext = os.path.splitext(name)
        src_lbl = os.path.join(src_lbl_dir, stem + ".txt")
        dst_lbl = os.path.join(dst_lbl_dir, stem + ".txt")
        if os.path.isfile(src_lbl) and not os.path.exists(dst_lbl):
            shutil.copy2(src_lbl, dst_lbl)

    if not val_imgs and train_imgs:
        _copy_one(img_train, lbl_train, img_val, lbl_val, train_imgs[0])
        print(
            f"Hinweis: images/val was empty — copied {train_imgs[0]} from train "
            "so Ultralytics validation can run",
            file=sys.stderr,
        )
    elif not train_imgs and val_imgs:
        _copy_one(img_val, lbl_val, img_train, lbl_train, val_imgs[0])
        print(
            f"Hinweis: images/train was empty — copied {val_imgs[0]} from val",
            file=sys.stderr,
        )

    return (
        len(_list_split_images(img_train)),
        len(_list_split_images(img_val)),
    )


def track_center_path(video_path, roi, max_frames=None, cache_dir=None,
                       camera_compensation=True, scene_cut_detection=True,
                       start_frame=0):
    """Liefert (timestamps_ms, x_centers, y_centers, frame_size) - track_roi
    gibt öffentlich nur EINE Achse zurück (je nach axis-Parameter), für eine
    Bounding-Box braucht es beide. Trackt darum zweimal (x, y) statt
    generate_funscript.py's getestete track_roi()/track_roi_cached()
    anzufassen, um deren Vertrag und Tests unverändert zu lassen - einmalige
    Mehrkosten für einen Offline-Datensatzlauf, nicht Teil des
    interaktiven Erzeugungspfads."""
    import generate_funscript as g
    ts, y_centers, frame_size, _cuts, _stats = g.track_roi_cached(
        video_path, roi, max_frames=max_frames, cache_dir=cache_dir,
        camera_compensation=camera_compensation,
        scene_cut_detection=scene_cut_detection, axis="y",
        start_frame=start_frame)
    _ts2, x_centers, _fs2, _cuts2, _stats2 = g.track_roi_cached(
        video_path, roi, max_frames=max_frames, cache_dir=cache_dir,
        camera_compensation=camera_compensation,
        scene_cut_detection=scene_cut_detection, axis="x",
        start_frame=start_frame)
    return ts, x_centers, y_centers, frame_size


def _box_to_yolo_label(cx, cy, box_w, box_h, width, height):
    """Wandelt eine Boxmitte+Größe in Pixeln in eine YOLO-Labelzeile
    (xc,yc,w,h, alle 0..1 relativ zur Bildgröße) um, geklammert an die
    Bildgrenzen. Gibt None zurück, wenn die geklammerte Box entartet ist
    (Mittelpunkt liegt vollständig außerhalb des Bildes) - dieser Frame wird
    dann übersprungen statt einer leeren/negativen Box."""
    x0 = max(0.0, cx - box_w / 2.0)
    y0 = max(0.0, cy - box_h / 2.0)
    x1 = min(float(width), cx + box_w / 2.0)
    y1 = min(float(height), cy + box_h / 2.0)
    if x1 <= x0 or y1 <= y0:
        return None
    xc_n = (x0 + x1) / 2.0 / width
    yc_n = (y0 + y1) / 2.0 / height
    w_n = (x1 - x0) / width
    h_n = (y1 - y0) / height
    return xc_n, yc_n, w_n, h_n


def _video_stem(video_path):
    """Kurzer, stabiler Dateiname-Präfix - Hash statt Klartext-Videoname,
    damit Sonderzeichen/Länge im Originalnamen keine Rolle spielen."""
    h = hashlib.sha1(os.path.abspath(video_path).encode("utf-8")).hexdigest()[:10]
    base = os.path.splitext(os.path.basename(video_path))[0]
    safe = "".join(c if c.isalnum() else "_" for c in base)[:40]
    return f"{safe}_{h}"


def _frame_label_lines(regions_at_frame, width, height):
    """Reiner Teil des Mehrregionen-Labelns: regions_at_frame ist eine Liste
    (class_id, cx, cy, box_w, box_h) - eine je Region IM SELBEN Frame - und
    liefert die fertigen YOLO-Zeilen (als Strings) dafür. Regionen, deren
    geklammerte Box entartet (siehe _box_to_yolo_label), werden einzeln
    übersprungen statt den ganzen Frame zu verwerfen - eine Region kann kurz
    aus dem Bild laufen, während die andere weiter sichtbar bleibt.

    Der Grund, das als eigene Funktion herauszuziehen: mehrere Regionen pro
    Bild (z.B. Eichel UND Brustwarze für Tf/Tj) sind der springende Punkt,
    warum das hier überhaupt gebraucht wird - ein Detektor, der beide
    Kontaktpunkte gleichzeitig in einem Bild erkennt, wie im FunGen2-
    Vergleichsscreenshot (Chat, 16. September 2026: "breast"/"hand"/"penis"
    gleichzeitig erkannt) - und das lässt sich so ohne Video/Tracking testen."""
    lines = []
    for class_id, cx, cy, box_w, box_h in regions_at_frame:
        label = _box_to_yolo_label(cx, cy, box_w, box_h, width, height)
        if label is None:
            continue
        xc_n, yc_n, w_n, h_n = label
        lines.append(f"{class_id} {xc_n:.6f} {yc_n:.6f} {w_n:.6f} {h_n:.6f}")
    return lines


def _scaled_box_wh(roi, box_scale, frame_w, frame_h):
    """Apply box_scale around the marked ROI size, clamped to the frame.
    Bootstrap keeps a fixed w/h per track (tracker only moves the center);
    a slight pad (1.05–1.2) helps YOLO learn a margin around the contact
    region. Values <1 shrink; 1.0 is the historical default."""
    scale = float(box_scale) if box_scale is not None else 1.0
    if scale <= 0:
        scale = 1.0
    _x, _y, w, h = roi
    w = max(8, int(round(w * scale)))
    h = max(8, int(round(h * scale)))
    w = min(w, max(8, int(frame_w)))
    h = min(h, max(8, int(frame_h)))
    return w, h


def build_dataset(video_path, regions, output_dir, sample_every=12,
                   max_frames=None, cache_dir=None, val_fraction=0.15, seed=0,
                   sample_prefix=None, start_frame=0, box_scale=1.0):
    """Track one or more regions through the video; write every sample_every-th
    frame plus one YOLO label per region (fixed box size × box_scale,
    center = tracked position) into output_dir.

    regions: list of (roi, class_id). One region as before; two for Tf/Tj-
    style content (both as lines in the same label file).

    sample_every=12 ≈ 0.5s at 25fps — denser sampling mostly adds correlated
    near-duplicates. start_frame skips intro (GUI seek).
    box_scale: multiply marked w/h (default 1.0). Prefer correcting bad
    samples in the review UI over guessing a large scale.
    """
    regions = [(tuple(int(v) for v in roi), class_id) for roi, class_id in regions]
    print(f"Tracke {video_path} ({len(regions)} Region(en))...", file=sys.stderr)
    tracks = []
    width = height = None
    for roi, class_id in regions:
        _ts, x_centers, y_centers, (width, height) = track_center_path(
            video_path, roi, max_frames=max_frames, cache_dir=cache_dir,
            start_frame=start_frame)
        tracks.append((roi, class_id, x_centers, y_centers))
    n = min(len(xc) for (_roi, _cid, xc, _yc) in tracks)
    print(f"{n} Frames getrackt, Videogröße {width}x{height}", file=sys.stderr)
    if abs(float(box_scale) - 1.0) > 1e-6:
        print(f"Box-Skalierung: {box_scale:.3f}× markierte Größe", file=sys.stderr)

    img_train_dir = os.path.join(output_dir, "images", "train")
    img_val_dir = os.path.join(output_dir, "images", "val")
    lbl_train_dir = os.path.join(output_dir, "labels", "train")
    lbl_val_dir = os.path.join(output_dir, "labels", "val")
    for d in (img_train_dir, img_val_dir, lbl_train_dir, lbl_val_dir):
        os.makedirs(d, exist_ok=True)

    stem = sample_prefix or _video_stem(video_path)
    rng = random.Random(seed)

    import cv2

    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Video konnte nicht geöffnet werden: {video_path}")
    if start_frame > 0:
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)
        print(f"Start bei Frame {start_frame}", file=sys.stderr)

    written = 0
    frame_idx = 0
    sample_idx = 0
    try:
        while frame_idx < n:
            ok, frame = cap.read()
            if not ok:
                break
            if frame_idx % sample_every == 0:
                regions_at_frame = []
                for roi, class_id, xc, yc in tracks:
                    bw, bh = _scaled_box_wh(roi, box_scale, width, height)
                    regions_at_frame.append(
                        (class_id, xc[frame_idx], yc[frame_idx], bw, bh))
                lines = _frame_label_lines(regions_at_frame, width, height)
                if not lines:
                    frame_idx += 1
                    continue

                is_val = rng.random() < val_fraction
                img_dir = img_val_dir if is_val else img_train_dir
                lbl_dir = lbl_val_dir if is_val else lbl_train_dir

                name = f"{stem}_{sample_idx:06d}"
                cv2.imwrite(os.path.join(img_dir, name + ".jpg"), frame)
                with open(os.path.join(lbl_dir, name + ".txt"), "w") as f:
                    f.write("\n".join(lines) + "\n")
                written += 1
                sample_idx += 1
            frame_idx += 1
    finally:
        cap.release()

    print(f"{written} annotierte Frames geschrieben ({video_path})", file=sys.stderr)
    # Random val_fraction can leave val (or train) empty on small clips —
    # heal before data.yaml is written so training never sees an empty split.
    ensure_nonempty_splits(output_dir)
    return written


CLASS_REGISTRY_FILENAME = "classes.json"


def _load_class_registry(output_dir):
    path = os.path.join(output_dir, CLASS_REGISTRY_FILENAME)
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return json.load(f)


def _save_class_registry(output_dir, registry):
    path = os.path.join(output_dir, CLASS_REGISTRY_FILENAME)
    with open(path, "w") as f:
        json.dump(registry, f, indent=2, sort_keys=True)
    return path


def _assign_class_id(registry, class_name, class_id=None):
    """Reiner Teil der Klassenverwaltung (kein Dateizugriff): liefert
    (id, aktualisierte_registry) für class_name aus registry (Name -> ID).
    Ohne class_id wird eine bereits vorhandene ID wiederverwendet oder die
    nächste freie vergeben; mit class_id wird genau diese erzwungen. Gibt
    bei unveränderter Zuordnung dasselbe registry-Objekt zurück (nicht nur
    einen gleichen Wert), damit der Aufrufer per `is` erkennt, ob
    gespeichert werden muss."""
    if class_id is None:
        if class_name in registry:
            return registry[class_name], registry
        class_id = max(registry.values(), default=-1) + 1
    if registry.get(class_name) == class_id:
        return class_id, registry
    updated = dict(registry)
    updated[class_name] = class_id
    return class_id, updated


def register_class(output_dir, class_name, class_id=None):
    """Trägt class_name in <output_dir>/classes.json ein (legt die Datei bei
    Bedarf an) und liefert die tatsächlich verwendete ID zurück - siehe
    Moduldoc zu mehreren Inhaltskategorien im selben Datensatz."""
    registry = _load_class_registry(output_dir)
    resolved_id, updated = _assign_class_id(registry, class_name, class_id)
    if updated is not registry:
        os.makedirs(output_dir, exist_ok=True)
        _save_class_registry(output_dir, updated)
    return resolved_id


# --- Kontrolle/Korrektur bereits geschriebener Beispiele --------------------
#
# build_dataset() schreibt Labels aus CSRT-/Grid-Tracking - laut Moduldoc wird
# der Detektor nie zuverlässiger als dieser Tracker. Gemessen (16. September
# 2026, echter Clip, docs/NEXT.md): unser Zwei-Punkt-Tracking greift auf
# echtem Material teils daneben (falsches Objekt, Drift nach Kamerazoom).
# Blind bootstrappen würde diese Fehler direkt in die Trainingsdaten
# übernehmen. Die folgenden Funktionen sind reine Datei-Operationen (kein
# Video/Tracking nötig, darum ohne echtes Material testbar) für eine
# Kontrollansicht: Beispiele aus einem Bootstrap-Lauf auflisten, einzelne
# Boxen von Hand korrigieren oder verwerfen, bevor trainiert wird.

def _label_path_for_image(image_path):
    """images/<split>/name.jpg -> labels/<split>/name.txt - reine Pfadregel,
    kein Dateizugriff."""
    parts = list(Path(image_path).parts)
    try:
        idx = len(parts) - 1 - parts[::-1].index("images")
    except ValueError:
        raise ValueError(f"kein 'images'-Ordner im Pfad: {image_path}")
    parts[idx] = "labels"
    return str(Path(*parts).with_suffix(".txt"))


def read_label_file(label_path):
    """Liest eine YOLO-Labeldatei -> Liste aus (class_id:int, xc,yc,w,h:float).
    Fehlt die Datei (z.B. der Frame hatte keine gültige Box, siehe
    build_dataset), wird eine leere Liste geliefert statt eines Fehlers."""
    if not os.path.exists(label_path):
        return []
    boxes = []
    with open(label_path) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 5:
                continue
            cid, xc, yc, w, h = parts
            boxes.append((int(cid), float(xc), float(yc), float(w), float(h)))
    return boxes


def write_label_file(label_path, boxes):
    """Schreibt boxes (dieselbe Form wie read_label_file liefert) zurück -
    für Korrekturen aus der Kontrollansicht. Eine leere Liste schreibt eine
    leere Datei (der Frame bleibt Teil des Datensatzes, nur ohne Box - YOLO
    behandelt das als Negativbeispiel), löscht die Datei aber NICHT - dafür
    ist discard_sample() da, das auch das Bild mit entfernt."""
    os.makedirs(os.path.dirname(label_path), exist_ok=True)
    with open(label_path, "w") as f:
        for cid, xc, yc, w, h in boxes:
            f.write(f"{int(cid)} {xc:.6f} {yc:.6f} {w:.6f} {h:.6f}\n")


def discard_sample(image_path):
    """Entfernt ein Beispiel vollständig (Bild + Labeldatei) - für Frames,
    die die Kontrollansicht als falsch verwirft (z.B. Box auf dem falschen
    Objekt), statt sie mit einer leeren/falschen Box im Datensatz zu lassen."""
    label_path = _label_path_for_image(image_path)
    for path in (image_path, label_path):
        if os.path.exists(path):
            os.remove(path)


def list_samples(output_dir, video_path=None, prefix=None):
    """Listet Beispiele aus <output_dir>/images/{train,val} auf, mit ihren
    Boxen - Grundlage der Kontrollansicht nach einem Bootstrap-Lauf.
    Schränkt die Liste ein auf die Beispiele GENAU eines Aufrufs, damit die
    Kontrolle nicht jedes Mal den gesamten - möglicherweise über viele
    Videos gewachsenen - Datensatz zeigt, sondern nur das gerade frisch
    Hinzugekommene: entweder über einen direkt übergebenen prefix (die GUI
    kennt ihren eigenen --sample-prefix aus dem Bootstrap-Aufruf bereits und
    muss ihn nicht neu herleiten) oder über video_path (leitet denselben
    Namenspräfix her, den _video_stem() beim Schreiben vergeben hat - nur
    für den CLI-Fall ohne festen --sample-prefix)."""
    if prefix is None and video_path:
        prefix = _video_stem(video_path)
    samples = []
    for split in ("train", "val"):
        img_dir = os.path.join(output_dir, "images", split)
        if not os.path.isdir(img_dir):
            continue
        for name in sorted(os.listdir(img_dir)):
            if not name.lower().endswith((".jpg", ".jpeg", ".png")):
                continue
            if prefix and not name.startswith(prefix):
                continue
            image_path = os.path.join(img_dir, name)
            label_path = _label_path_for_image(image_path)
            samples.append({
                "split": split,
                "name": name,
                "image": image_path,
                "label": label_path,
                "boxes": read_label_file(label_path),
            })
    return samples


def write_data_yaml(output_dir, class_names=None):
    """Ultralytics-Datensatzkonfiguration - siehe
    https://docs.ultralytics.com/datasets/detect/#dataset-yaml-format.
    Ohne class_names werden die Namen aus classes.json übernommen (nach ID
    sortiert) - damit data.yaml immer ALLE über mehrere Aufrufe hinweg
    angelegten Kategorien enthält, nicht nur die des letzten Aufrufs."""
    if class_names is None:
        registry = _load_class_registry(output_dir) or {"motion_region": 0}
        class_names = [name for name, _id in sorted(registry.items(), key=lambda kv: kv[1])]
    path = os.path.join(output_dir, "data.yaml")
    abs_dir = os.path.abspath(output_dir)
    lines = [
        f"path: {abs_dir}",
        "train: images/train",
        "val: images/val",
        "names:",
    ]
    for i, name in enumerate(class_names):
        lines.append(f"  {i}: {name}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Geschrieben: {path}", file=sys.stderr)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", required=True, help="Pfad zum Video")
    ap.add_argument("--roi", required=True, metavar="x,y,w,h",
                     help="Start-ROI, von Hand markiert oder aus einem früheren Lauf übernommen")
    ap.add_argument("--output-dir", required=True, help="Zielordner für den YOLO-Datensatz")
    ap.add_argument("--sample-every", type=int, default=12,
                     help="Nur jeden N-ten Frame als Trainingsbeispiel schreiben (Standard: 12)")
    ap.add_argument("--max-frames", type=int, default=None)
    ap.add_argument("--cache-dir", default=None,
                     help="Wie generate_funscript.py --cache-dir - vermeidet erneutes Tracking bei Wiederholung")
    ap.add_argument("--val-fraction", type=float, default=0.15)
    ap.add_argument("--class-id", type=int, default=None,
                     help="Feste Klassen-ID erzwingen - normalerweise nicht nötig, "
                          "die ID wird über classes.json automatisch je --class-name vergeben/wiederverwendet")
    ap.add_argument("--class-name", default="motion_region",
                     help="Inhaltskategorie dieses Clips (z.B. 'blowjob', 'tf_tj_mix') - "
                          "mehrere Kategorien im selben --output-dir werden zu eigenen Klassen kombiniert")
    ap.add_argument("--roi2", default=None, metavar="x,y,w,h",
                     help="Zweite Region (z.B. Eichel+Brustwarze für Tf/Tj) - optional, beide landen "
                          "als zwei Klassen im selben Bild statt zwei getrennten Datensätzen")
    ap.add_argument("--class-name2", default=None,
                     help="Klasse der zweiten Region (--roi2) - erforderlich, wenn --roi2 gesetzt ist")
    ap.add_argument("--roi3", default=None, metavar="x,y,w,h",
                     help="Dritte Region im selben Frame (optional)")
    ap.add_argument("--class-name3", default=None,
                     help="Klasse der dritten Region (--roi3)")
    ap.add_argument("--roi4", default=None, metavar="x,y,w,h",
                     help="Vierte Region im selben Frame (optional)")
    ap.add_argument("--class-name4", default=None,
                     help="Klasse der vierten Region (--roi4)")
    ap.add_argument("--roi5", default=None, metavar="x,y,w,h",
                     help="Fifth region on the same frame (optional)")
    ap.add_argument("--class-name5", default=None,
                     help="Class for --roi5")
    ap.add_argument("--roi6", default=None, metavar="x,y,w,h",
                     help="Sixth region on the same frame (optional)")
    ap.add_argument("--class-name6", default=None,
                     help="Class for --roi6")
    ap.add_argument("--roi7", default=None, metavar="x,y,w,h",
                     help="Seventh region on the same frame (optional)")
    ap.add_argument("--class-name7", default=None,
                     help="Class for --roi7")
    ap.add_argument("--roi8", default=None, metavar="x,y,w,h",
                     help="Eighth region on the same frame (optional)")
    ap.add_argument("--class-name8", default=None,
                     help="Class for --roi8")
    ap.add_argument("--roi9", default=None, metavar="x,y,w,h",
                     help="Ninth region on the same frame (optional)")
    ap.add_argument("--class-name9", default=None,
                     help="Class for --roi9")
    # Free-tag capacity > taxonomy (nipples×2 + full set). Keep CLI in sync with
    # bodyparts.MAX_REGIONS_PER_IMAGE / MaxRegionsPerImage.
    ap.add_argument("--roi10", default=None, metavar="x,y,w,h",
                     help="Tenth free-tag region (optional)")
    ap.add_argument("--class-name10", default=None, help="Class for --roi10")
    ap.add_argument("--roi11", default=None, metavar="x,y,w,h",
                     help="Eleventh free-tag region (optional)")
    ap.add_argument("--class-name11", default=None, help="Class for --roi11")
    ap.add_argument("--roi12", default=None, metavar="x,y,w,h",
                     help="Twelfth free-tag region (optional)")
    ap.add_argument("--class-name12", default=None, help="Class for --roi12")
    ap.add_argument("--sample-prefix", default=None,
                     help="Fester Dateinamenspräfix statt des automatischen Video-Hash-Präfix - "
                          "so kann ein Aufrufer (z.B. die GUI) den Präfix vorher selbst bestimmen "
                          "und später list_samples()/Kontrollansicht ohne erneute Hash-Berechnung "
                          "genau auf diesen Lauf eingrenzen")
    ap.add_argument("--start-seconds", type=float, default=0.0,
                     help="Überspringt die ersten N Sekunden (GUI-Seek am schwarzen Intro)")
    ap.add_argument("--box-scale", type=float, default=1.0,
                     help="Multiply marked ROI width/height for YOLO labels "
                          "(1.0 = exact mark; 1.1–1.2 adds a small pad). "
                          "Still prefer correcting boxes in the review UI.")
    args = ap.parse_args()

    try:
        roi = tuple(int(v) for v in args.roi.split(","))
        if len(roi) != 4:
            raise ValueError
    except ValueError:
        ap.error('--roi muss "x,y,w,h" sein')

    regions = [(roi, register_class(args.output_dir, args.class_name, class_id=args.class_id))]

    def _add_roi(flag, roi_s, class_s):
        if not roi_s:
            return
        if not class_s:
            ap.error(f"{flag} braucht einen Klassennamen")
        try:
            box = tuple(int(v) for v in roi_s.split(","))
            if len(box) != 4:
                raise ValueError
        except ValueError:
            ap.error(f'{flag} muss "x,y,w,h" sein')
        regions.append((box, register_class(args.output_dir, class_s)))

    for n in range(2, 13):
        _add_roi(f"--roi{n}", getattr(args, f"roi{n}", None),
                 getattr(args, f"class_name{n}", None))

    start_frame = 0
    if getattr(args, "start_seconds", 0) and args.start_seconds > 0:
        import cv2
        _cap = cv2.VideoCapture(args.video)
        _fps = _cap.get(cv2.CAP_PROP_FPS) or 25.0
        _cap.release()
        start_frame = max(0, int(round(args.start_seconds * _fps)))
        if start_frame:
            print(f"Start bei {args.start_seconds:.2f}s (Frame {start_frame})", file=sys.stderr)

    build_dataset(args.video, regions, args.output_dir,
                  sample_every=args.sample_every, max_frames=args.max_frames,
                  cache_dir=args.cache_dir, val_fraction=args.val_fraction,
                  sample_prefix=args.sample_prefix, start_frame=start_frame,
                  box_scale=args.box_scale)
    ensure_nonempty_splits(args.output_dir)
    write_data_yaml(args.output_dir)


if __name__ == "__main__":
    main()
