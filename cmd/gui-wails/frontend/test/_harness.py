"""Gemeinsame Hilfsfunktionen für die Frontend-Tests.

Der Grund für diese Datei: die Tests laden das echte JavaScript und ersetzen
nur die wailsjs-Bindings durch Stubs. Diese Stub-Listen wurden bisher in
jedem Test von Hand gepflegt - und veralteten damit bei JEDER neuen
Go-Methode. Der Test schlug dann mit "does not provide an export named ..."
fehl, also aus einem Grund, der mit dem Prüfgegenstand nichts zu tun hat.
Zweimal ist genau das passiert.

Hier wird die Stub-Liste stattdessen aus der tatsächlich erzeugten
App.js gelesen. Neue Bindings sind damit automatisch enthalten.
"""

import functools
import http.server
import os
import pathlib
import re
import threading

FRONTEND = pathlib.Path(__file__).resolve().parent

RUNTIME_STUB = """
const handlers = {};
export function EventsOn(name, cb) { (handlers[name] ||= []).push(cb); }
export function EventsEmit() {}
window.__triggerEvent = (name, data) => (handlers[name] || []).forEach(cb => cb(data));
"""


def app_stub(overrides=None):
    """Erzeugt einen Stub für ALLE echten Bindings.

    overrides bildet Bindingnamen auf JavaScript-Ausdrücke ab - für die
    Aufrufe, deren Verhalten der Test tatsächlich braucht. Alles andere wird
    zu einer harmlosen Funktion, die ein leeres Objekt liefert.
    """
    bindings = FRONTEND.parent / "wailsjs" / "go" / "main" / "App.js"
    names = sorted(set(re.findall(r"export function (\w+)", bindings.read_text())))
    if not names:
        raise RuntimeError(
            f"Keine Bindings in {bindings} gefunden - wurde 'wails build' schon ausgeführt?")

    overrides = overrides or {}
    # Everyday Generate auto-finds tip after video load. Stub must emit
    # generate:autoroi or buttons stay disabled forever in Playwright tests.
    if "AutoDetectROI" not in overrides:
        overrides["AutoDetectROI"] = (
            "async (path, engine) => { "
            "setTimeout(() => window.__triggerEvent && window.__triggerEvent("
            "'generate:autoroi', {x:40,y:40,w:120,h:120,engine: engine || 'auto',"
            "videoPath: path, seq: 1}), 0); }"
        )
    lines = ["window.__calls = window.__calls || [];"]
    for name in names:
        lines.append(f"export const {name} = {overrides.get(name, 'async () => ({})')};")
    # Overrides für Namen, die (noch) nicht in den Bindings stehen, trotzdem
    # aufnehmen - sonst schlägt ein Test still fehl, weil sein Stub fehlt.
    for name, body in overrides.items():
        if name not in names:
            lines.append(f"export const {name} = {body};")
    return "\n".join(lines)


def make_handler(app_stub_code):
    class Handler(http.server.SimpleHTTPRequestHandler):
        STUBS = {
            "/wailsjs/go/main/App": app_stub_code,
            "/wailsjs/runtime/runtime": RUNTIME_STUB,
            # main.js macht "import './style.css'" - das ist eine Vite-
            # Eigenheit, die ein Browser nicht kennt. Im Test als leeres
            # Modul ausliefern, sonst scheitert der gesamte Import und der
            # Test schlägt aus einem Grund fehl, der nichts mit dem
            # Prüfgegenstand zu tun hat.
            "/src/style.css": "export default '';",
            "/src/theme_flow.css": "export default '';",
        }

        def do_GET(self):
            stub = self.STUBS.get(self.path)
            # Every CSS module import ("import './x.css'") gets the same empty
            # stub, so a new stylesheet imported from any module (C1/C2 Flow
            # CSS) cannot break the app start in tests. "?real" requests are
            # the <link> stylesheets and stay real files.
            if stub is None and self.path.startswith("/src/") and self.path.endswith(".css"):
                stub = "export default '';"
            if stub is not None:
                body = stub.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/javascript")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            self.serve_file_with_range_support()

        def serve_file_with_range_support(self):
            """Wie SimpleHTTPRequestHandler.do_GET, aber mit HTTP-Range-
            Unterstützung (RFC 7233) - ohne die reagiert ein <video>-Element
            im Browser gar nicht auf Suchen/Seeken: seekable bleibt leer
            ([0, 0]), weil der Browser dafür gezielt Byte-Bereiche
            nachladen muss, nicht die ganze Datei. Betraf zuerst einen
            neuen Test, der prüfen wollte, ob der Kurven-Editor beim Ziehen
            eines Punkts das Video mitspult - stellte sich als Lücke in
            dieser gemeinsamen Testinfrastruktur heraus, nicht im
            geprüften Code, und betrifft daher jeden künftigen
            video-seitigen Test genauso.
            """
            path = self.translate_path(self.path)
            if os.path.isdir(path):
                super().do_GET()
                return
            if not os.path.isfile(path):
                self.send_error(404)
                return
            file_size = os.path.getsize(path)
            ctype = self.guess_type(path)
            start, end, status = 0, file_size - 1, 200
            range_header = self.headers.get("Range")
            if range_header:
                match = re.match(r"bytes=(\d*)-(\d*)", range_header)
                if not match:
                    self.send_error(416)
                    return
                start_s, end_s = match.groups()
                if start_s == "" and end_s != "":
                    # Suffix-Bereich: die letzten N Bytes.
                    length = int(end_s)
                    start, end = max(0, file_size - length), file_size - 1
                else:
                    start = int(start_s) if start_s else 0
                    end = int(end_s) if end_s else file_size - 1
                end = min(end, file_size - 1)
                if start > end or start >= file_size:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{file_size}")
                    self.end_headers()
                    return
                status = 206
            length = end - start + 1
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Accept-Ranges", "bytes")
            if status == 206:
                self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
            self.send_header("Content-Length", str(length))
            self.end_headers()
            with open(path, "rb") as f:
                f.seek(start)
                remaining = length
                while remaining > 0:
                    chunk = f.read(min(65536, remaining))
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    remaining -= len(chunk)

        def log_message(self, *args):
            pass

    return Handler


def serve(app_stub_code):
    """Startet den Testserver und liefert (basis_url, herunterfahren)."""
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(make_handler(app_stub_code),
                                            directory=str(FRONTEND.parent))
    )
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{server.server_address[1]}", server.shutdown


class Checker:
    """Sammelt Prüfergebnisse und gibt sie einheitlich aus."""

    def __init__(self):
        self.failures = []

    def __call__(self, name, condition, detail=""):
        print(("  OK   " if condition else "  FAIL ") + name
              + (f"  [{detail}]" if not condition else ""))
        if not condition:
            self.failures.append(name)

    def report(self):
        print(("FEHLGESCHLAGEN: " + ", ".join(self.failures)) if self.failures
              else "Alle Prüfungen bestanden.")
        return 1 if self.failures else 0
