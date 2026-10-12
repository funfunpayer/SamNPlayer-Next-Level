"""Test für ai_quality.py - die optionale Colibri-Zweitmeinung zur
Qualitätsbewertung.

Läuft komplett OHNE einen laufenden Colibri-Server: build_prompt() und
parse_response() sind reine Funktionen, suggest_quality() nimmt über
_post_fn eine Attrappe entgegen (derselbe Trick wie in ai_profile_test.py).

Ausführen: python3 generator/ai_quality_test.py
"""

import sys

import ai_quality


def main():
    failures = []

    def check(name, cond, detail=""):
        print(("  OK   " if cond else "  FAIL ") + name + (f"  [{detail}]" if not cond else ""))
        if not cond:
            failures.append(name)

    # --- build_prompt: übernimmt die Werte des klassischen Checkers ----------
    metrics = {"concentration": 0.8, "gap_fraction": 0.1}
    messages = ai_quality.build_prompt(
        metrics, warnings=["Rhythmus schwach"], score=0.72, rule_passed=True)
    check("zwei Nachrichten (System + User)", len(messages) == 2, str(len(messages)))
    check("Systemprompt nennt das exakte REPORT_VERDICTS-Vokabular",
          all(v in messages[0]["content"] for v in ai_quality.VERDICTS), "")
    check("Nutzerprompt enthält passed/score/metrics/warnings",
          all(s in messages[1]["content"] for s in
              ["passed = True", "score = 0.720", "concentration = 0.8", "Rhythmus schwach"]),
          messages[1]["content"])

    empty = ai_quality.build_prompt({}, warnings=None)
    check("ohne Warnungen: expliziter Hinweis statt leerer Liste",
          "warnings: none" in empty[1]["content"], empty[1]["content"])

    # --- parse_response: striktes Vokabular, kein Raten ----------------------
    ok = ai_quality.parse_response('{"verdict": "grenzwertig", "reason": "Rhythmus flach"}')
    check("gültige Antwort wird geparst",
          ok == {"verdict": "grenzwertig", "reason": "Rhythmus flach"}, str(ok))
    check("englisches/erfundenes Vokabular wird verworfen, nicht übersetzt",
          ai_quality.parse_response('{"verdict": "usable", "reason": "x"}') is None, "")
    check("kaputtes JSON wird verworfen", ai_quality.parse_response("kein JSON") is None, "")

    # --- available(): ehrliche Antwort ohne laufenden Server -----------------
    check("available() meldet False ohne Server",
          ai_quality.available(base_url="http://127.0.0.1:1", timeout=0.5) is False, "")

    # --- suggest_quality() mit injizierter Server-Attrappe --------------------
    def fake_post_ok(payload):
        return {"choices": [{"message": {
            "content": '{"verdict": "unbrauchbar", "reason": "zu viele Lücken"}'}}]}

    result = ai_quality.suggest_quality(metrics, rule_passed=False, _post_fn=fake_post_ok)
    check("suggest_quality liefert die geparste Antwort der Attrappe",
          result is not None and result["verdict"] == "unbrauchbar", str(result))

    def fake_post_malformed(payload):
        return {"choices": [{"message": {"content": "kein JSON"}}]}

    check("suggest_quality liefert None bei kaputter Modellantwort statt zu raten",
          ai_quality.suggest_quality(metrics, _post_fn=fake_post_malformed) is None, "")

    def fake_post_unreachable(payload):
        raise ConnectionRefusedError("kein Server")

    check("suggest_quality liefert None statt zu crashen, wenn der Server nicht antwortet",
          ai_quality.suggest_quality(metrics, _post_fn=fake_post_unreachable) is None, "")

    print(("FEHLGESCHLAGEN: " + ", ".join(failures)) if failures else "Alle Prüfungen bestanden.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
