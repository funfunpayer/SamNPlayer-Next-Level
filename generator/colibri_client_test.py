"""Test für colibri_client.py - den gemeinsamen HTTP-Client, den
ai_profile.py und ai_quality.py teilen.

Läuft ohne laufenden Colibri-Server: available() wird gegen einen echten,
garantiert nicht belegten Port geprüft (reale Negativprobe, keine
Attrappe), chat() über eine injizierte _post_fn (derselbe Trick wie in den
übrigen ai_*_test.py-Dateien).

Ausführen: python3 generator/colibri_client_test.py
"""

import sys

import colibri_client


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- available(): echte Negativprobe, kein Server auf Port 1 -------------
    check("available() meldet False ohne laufenden Server",
          colibri_client.available(base_url="http://127.0.0.1:1", timeout=0.5) is False, "")

    # --- chat(): reicht Nachrichten und Modell korrekt durch ------------------
    seen = {}

    def fake_post(payload):
        seen.update(payload)
        return {"choices": [{"message": {"content": "Antwort"}}]}

    reply = colibri_client.chat(
        [{"role": "user", "content": "hallo"}], model="mein-modell", _post_fn=fake_post)
    check("chat() liefert den Antworttext der Attrappe", reply == "Antwort", reply)
    check("chat() reicht die Nachrichten unverändert durch",
          seen.get("messages") == [{"role": "user", "content": "hallo"}], str(seen))
    check("chat() reicht das Modell durch", seen.get("model") == "mein-modell", str(seen))

    # --- chat(): wirft weiter, statt den Fehler zu verschlucken ---------------
    def fake_post_broken(payload):
        raise ValueError("kaputte Antwort")

    raised = False
    try:
        colibri_client.chat([{"role": "user", "content": "x"}], _post_fn=fake_post_broken)
    except ValueError:
        raised = True
    check("chat() wirft weiter statt den Fehler zu verschlucken "
          "(Aufrufer entscheiden, wie sie das behandeln)", raised, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
