"""Backend-Register: Analyseverfahren als austauschbare Bausteine.

Bisher war die Wahl des Analyseverfahrens eine fest verdrahtete
Fallunterscheidung mitten in der Pipeline. Jedes neue Verfahren hätte dort
einen Eingriff bedeutet - an einer Stelle, die mit dem Verfahren selbst
nichts zu tun hat.

Hier ist stattdessen ein Register mit einem festen Vertrag. Wer ein neues
Verfahren beisteuert, schreibt eine Funktion und meldet sie an; an der
Pipeline ändert sich nichts.

DER VERTRAG
-----------

    analyze(video_path, roi, options) -> (timestamps_ms, positions,
                                          frame_size, scene_cuts, stats)

    timestamps_ms  ndarray, aufsteigend, Millisekunden
    positions      ndarray gleicher Länge, BILDKOORDINATEN (nicht 0-100)
    frame_size     (breite, hoehe) in Pixeln
    scene_cuts     Liste von Frameindizes, darf leer sein
    stats          dict mit mindestens tracker_lost_frames, total_frames,
                   vertical_range

Zwei Bedingungen sind nicht Formsache, sondern tragen die Auswertung:

Die Positionen kommen in Bildkoordinaten, NICHT normalisiert. Die
Normalisierung gehört zur nachgelagerten Verarbeitung und kennt Optionen wie
die gleitende Dynamik. Ein Backend, das selbst normalisiert, würde diese
Schritte stillschweigend aushebeln.

stats["vertical_range"] ist die Amplitude in Pixeln. Der Quality Doctor
braucht sie, um echte Bewegung von hochskaliertem Zittern zu unterscheiden -
nach der Normalisierung ist diese Information unwiederbringlich weg.

EIGENE BACKENDS
---------------

Python-Dateien im Plugin-Verzeichnis werden beim Start geladen:

    from backends import register

    def analyze(video_path, roi, options):
        ...
        return timestamps, positions, (w, h), [], stats

    register("mein_verfahren", analyze, beschreibung="Was es tut")

Absichtlich keine Sandbox: ein Plugin läuft mit denselben Rechten wie das
Programm. Wer eines installiert, muss ihm vertrauen. Das ehrlich zu sagen
ist besser als eine Scheinsicherheit, die bei erster Gelegenheit umgangen
wird.
"""

import os
import sys
import traceback


_BACKENDS = {}


class BackendError(RuntimeError):
    """Fehler beim Laden oder Ausführen eines Backends."""


def register(name, func, beschreibung="", quelle="eingebaut"):
    """Meldet ein Analyseverfahren an.

    Ein bereits vergebener Name wird NICHT stillschweigend überschrieben:
    zwei Plugins mit demselben Namen wären sonst ein Fehler, der sich erst
    im Ergebnis zeigt und kaum zu finden ist.
    """
    if not callable(func):
        raise BackendError(f"Backend {name!r} ist keine aufrufbare Funktion")
    if name in _BACKENDS and _BACKENDS[name]["quelle"] != quelle:
        raise BackendError(
            f"Backend-Name {name!r} ist bereits vergeben "
            f"(von {_BACKENDS[name]['quelle']}) - bitte einen anderen wählen")
    _BACKENDS[name] = {"func": func, "beschreibung": beschreibung, "quelle": quelle}


def available():
    return {name: {"beschreibung": entry["beschreibung"], "quelle": entry["quelle"]}
            for name, entry in sorted(_BACKENDS.items())}


def get(name):
    if name not in _BACKENDS:
        known = ", ".join(sorted(_BACKENDS)) or "(keine)"
        raise BackendError(f"Unbekanntes Backend {name!r}. Verfügbar: {known}")
    return _BACKENDS[name]["func"]


def run(name, video_path, roi, options):
    """Führt ein Backend aus und prüft, ob es sich an den Vertrag hält.

    Die Prüfung ist nicht Zierde: ein Plugin, das eine Zeile zu wenig
    liefert oder bereits normalisierte Positionen zurückgibt, würde sonst
    erst viel später auffallen - als unerklärlich schlechtes Skript, nicht
    als Fehler im Plugin.
    """
    func = get(name)
    try:
        result = func(video_path, roi, options)
    except Exception as exc:
        raise BackendError(f"Backend {name!r} ist gescheitert: {exc}") from exc

    if not isinstance(result, tuple) or len(result) != 5:
        count = len(result) if hasattr(result, "__len__") else "?"
        raise BackendError(
            f"Backend {name!r} muss 5 Werte liefern (timestamps_ms, positions, "
            f"frame_size, scene_cuts, stats), bekam {type(result).__name__} "
            f"mit {count} Einträgen")

    timestamps, positions, frame_size, scene_cuts, stats = result
    if len(timestamps) != len(positions):
        raise BackendError(
            f"Backend {name!r}: {len(timestamps)} Zeitstempel, aber "
            f"{len(positions)} Positionen - das muss gleich lang sein")
    if len(timestamps) < 2:
        raise BackendError(f"Backend {name!r} lieferte zu wenige Messwerte")
    if not isinstance(stats, dict):
        raise BackendError(f"Backend {name!r}: stats muss ein dict sein")
    for key in ("tracker_lost_frames", "total_frames", "vertical_range"):
        if key not in stats:
            raise BackendError(
                f"Backend {name!r}: stats fehlt der Eintrag {key!r}. "
                "Er wird für die Qualitätsbewertung gebraucht.")
    if not (isinstance(frame_size, (tuple, list)) and len(frame_size) == 2):
        raise BackendError(f"Backend {name!r}: frame_size muss (breite, hoehe) sein")

    return timestamps, positions, tuple(frame_size), list(scene_cuts or []), stats


def default_plugin_dir():
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return os.path.join(local, "SamNPlayer", "plugins")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(xdg, "SamNPlayer", "plugins")


def load_plugins(directory=None, verbose=True):
    """Lädt Python-Dateien aus dem Plugin-Verzeichnis.

    Ein fehlerhaftes Plugin wird gemeldet und übersprungen, nicht
    weitergereicht: dass eine Erweiterung nicht lädt, darf das Programm
    nicht unbrauchbar machen.
    """
    directory = directory or default_plugin_dir()
    if not os.path.isdir(directory):
        return []

    import importlib.util

    # Damit ein Plugin "from backends import register" schreiben kann, muss
    # dieses Verzeichnis im Suchpfad stehen.
    own_dir = os.path.dirname(os.path.abspath(__file__))
    if own_dir not in sys.path:
        sys.path.insert(0, own_dir)

    loaded = []
    for filename in sorted(os.listdir(directory)):
        if not filename.endswith(".py") or filename.startswith("_"):
            continue
        path = os.path.join(directory, filename)
        module_name = "samnplayer_plugin_" + os.path.splitext(filename)[0]
        try:
            spec = importlib.util.spec_from_file_location(module_name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[module_name] = module
            spec.loader.exec_module(module)
            loaded.append(filename)
            if verbose:
                print(f"Plugin loaded: {filename}", file=sys.stderr)
        except Exception:
            if verbose:
                print(f"Plugin {filename} could not be loaded and will be "
                      f"skipped:\n{traceback.format_exc()}", file=sys.stderr)
    return loaded
