"""Test für ai_profile.py - den optionalen Colibri-Profilvorschlag.

Läuft komplett OHNE einen laufenden Colibri-Server: build_prompt() und
parse_response() sind reine Funktionen, und suggest_profile() nimmt über
_post_fn eine Attrappe entgegen (derselbe Injektionstrick wie
_run_model_fn in ai_roi_test.py).

Ausführen: python3 generator/ai_profile_test.py
"""

import sys

import ai_profile
import motion_signature


def make_signature(**overrides):
    sig = {field: 0.5 for field in motion_signature.SIGNATURE_FIELDS}
    sig.update(overrides)
    return sig


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- build_prompt: enthält alle Signaturfelder und System-Vertrag -------
    sig = make_signature(vertical_share=0.9, rhythm_strength=0.8)
    messages = ai_profile.build_prompt(sig)
    check("zwei Nachrichten (System + User)", len(messages) == 2, str(len(messages)))
    check("Systemprompt nennt alle erlaubten Profile",
          all(p in messages[0]["content"] for p in ai_profile.KNOWN_PROFILES), "")
    check("Nutzerprompt enthält die Signaturwerte",
          "vertical_share = 0.900" in messages[1]["content"], messages[1]["content"])
    check("ohne bekannte Beispiele: Hinweis statt Absturz",
          "No previously named examples" in messages[1]["content"], "")

    known = [{"label": "tj-szene-1", "signature": make_signature(vertical_share=0.9)}]
    messages_with_known = ai_profile.build_prompt(sig, known_examples=known)
    check("mit bekannten Beispielen: Label erscheint im Prompt",
          '"tj-szene-1"' in messages_with_known[1]["content"], messages_with_known[1]["content"])

    # --- parse_response: strikter Vertrag ------------------------------------
    ok = ai_profile.parse_response('{"profile": "tf", "confidence": 0.8, "reason": "eng"}')
    check("gültige Antwort wird geparst",
          ok == {"profile": "tf", "confidence": 0.8, "reason": "eng"}, str(ok))

    check("unbekanntes Profil wird verworfen, nicht geraten",
          ai_profile.parse_response('{"profile": "erfunden", "confidence": 0.9}') is None, "")
    check("'tj' wird verworfen - seit dem Dropdown-Merge kein eigenständiges Profil mehr,"
          " nur noch 'tf' (siehe KNOWN_PROFILES)",
          ai_profile.parse_response('{"profile": "tj", "confidence": 0.9}') is None, "")
    check("kaputtes JSON wird verworfen, nicht geraten",
          ai_profile.parse_response("das ist kein JSON") is None, "")
    check("Liste statt Objekt wird verworfen",
          ai_profile.parse_response("[1, 2, 3]") is None, "")
    unsure = ai_profile.parse_response('{"profile": "unsure", "confidence": 0.1, "reason": "?"}')
    check("'unsure' ist ein gültiges Ergebnis (kein Rateverbot umgangen)",
          unsure is not None and unsure["profile"] == "unsure", str(unsure))
    clamped = ai_profile.parse_response('{"profile": "tf", "confidence": 5.0, "reason": "x"}')
    check("Konfidenz wird auf 0..1 begrenzt", clamped["confidence"] == 1.0, str(clamped))

    # --- available(): ehrliche Antwort ohne laufenden Server -----------------
    check("available() meldet False, wenn kein Server auf dem Port lauscht",
          ai_profile.available(base_url="http://127.0.0.1:1", timeout=0.5) is False, "")

    # --- suggest_profile() mit injizierter Server-Attrappe --------------------
    def fake_post_ok(payload):
        check("Payload enthält die System+User-Nachrichten",
              len(payload["messages"]) == 2, str(payload))
        return {"choices": [{"message": {
            "content": '{"profile": "tf", "confidence": 0.7, "reason": "hoher vertical_share"}'
        }}]}

    result = ai_profile.suggest_profile(sig, _post_fn=fake_post_ok)
    check("suggest_profile liefert die geparste Antwort der Attrappe",
          result is not None and result["profile"] == "tf", str(result))

    def fake_post_malformed(payload):
        return {"choices": [{"message": {"content": "nicht JSON"}}]}

    check("suggest_profile liefert None bei kaputter Modellantwort statt zu raten",
          ai_profile.suggest_profile(sig, _post_fn=fake_post_malformed) is None, "")

    def fake_post_unreachable(payload):
        raise ConnectionRefusedError("kein Server")

    check("suggest_profile liefert None statt zu crashen, wenn der Server nicht antwortet",
          ai_profile.suggest_profile(sig, _post_fn=fake_post_unreachable) is None, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
