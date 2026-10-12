#!/usr/bin/env python3
"""ai_quality.py - asks a local Colibri LLM for a prose second opinion on
a generated script's quality, alongside (never instead of) the existing
non-AI Quality Doctor score.

Third and last step of docs/AI_ADAPTER.md's plan (region -> profile ->
quality). Same relationship to the non-AI system as the other two steps:

  1. `quality_doctor.evaluate()` measures the run (unchanged, no AI) and
     produces `passed`/`score`/`warnings`/`metrics` - the SAME dict this
     module reads from, not a re-measurement.
  2. `quality_model.py` can additionally score it with a learned model,
     but only once it has beaten the fixed rules in cross-validation on a
     real labelled corpus (`MIN_SAMPLES`/`MIN_PER_CLASS`).
  3. This module never becomes a third silent decision-maker. It asks the
     LLM to use the SAME vocabulary a human already uses in this project's
     own feedback loop - `REPORT_VERDICTS` in generate_funscript.py
     ("brauchbar" / "grenzwertig" / "unbrauchbar") - plus one sentence of
     reasoning. Nothing here writes `quality["passed"]`, touches
     `--feedback`, or feeds quality_model.train() automatically: an LLM
     opinion sitting next to the numbers in the report is meant for a
     human to read, not for the pipeline to act on. Turning it into real
     feedback stays a `--feedback` call a person makes, same as today.

WHY NOT A TRAINED CLASSIFIER: quality_model.py already IS the trained-model
path, and it has the safeguard this project insists on (cross-validated,
only adopted if it beats the rules). Bolting a second trained model
alongside it would just be two black boxes instead of one. A zero-shot LLM
opinion needs no training corpus and can reason in prose about WHY a run
looks weak - restoring, informally, some of the readability
quality_model.py buys formally by avoiding a neural net.
"""

import sys

import colibri_client

VERDICTS = ("brauchbar", "grenzwertig", "unbrauchbar")

_SYSTEM_PROMPT = (
    "You give a second opinion on an automatically generated funscript's "
    "quality. You do not see the video, only the same measured numbers, "
    "warnings, and score a classical (non-AI) quality checker already "
    "computed. Reply with ONLY a single JSON object: "
    '{"verdict": one of ["brauchbar", "grenzwertig", "unbrauchbar"], '
    '"reason": one short sentence}. Use the checker\'s own pass/fail as a '
    "strong prior - only disagree with it when the numbers themselves "
    "give a concrete reason, and say what that reason is."
)


def build_prompt(metrics, warnings=None, score=None, rule_passed=None):
    """Pure function: Quality Doctor's own output -> chat messages.

    Testable without a network call. metrics/warnings/score/rule_passed
    are read directly from quality_doctor.evaluate()'s return value -
    this module does not recompute anything.
    """
    lines = ["Classical quality checker result:"]
    if rule_passed is not None:
        lines.append(f"  passed = {bool(rule_passed)}")
    if score is not None:
        lines.append(f"  score = {float(score):.3f}")
    if metrics:
        lines.append("  metrics:")
        for key, value in sorted(metrics.items()):
            lines.append(f"    {key} = {value}")
    if warnings:
        lines.append("  warnings:")
        for warning in warnings:
            lines.append(f"    - {warning}")
    else:
        lines.append("  warnings: none")

    return [
        {"role": "system", "content": _SYSTEM_PROMPT},
        {"role": "user", "content": "\n".join(lines)},
    ]


def parse_response(raw_text):
    """Pure function: raw model text -> {"verdict", "reason"} or None.

    Deliberately strict, same reasoning as ai_profile.parse_response: an
    out-of-contract reply is treated as no answer, not coerced into one.
    """
    import json

    try:
        data = json.loads(raw_text)
    except (json.JSONDecodeError, TypeError):
        return None
    if not isinstance(data, dict):
        return None
    verdict = data.get("verdict")
    if verdict not in VERDICTS:
        return None
    reason = str(data.get("reason", "")).strip()
    return {"verdict": verdict, "reason": reason}


def available(base_url=None, timeout=1.5):
    """Re-exported for convenience so callers only need `import ai_quality`.
    See colibri_client.available() for the actual behavior."""
    return colibri_client.available(base_url=base_url, timeout=timeout)


def suggest_quality(metrics, warnings=None, score=None, rule_passed=None,
                     base_url=None, model=None, timeout=colibri_client.DEFAULT_TIMEOUT_S,
                     _post_fn=None):
    """Ask the local LLM for a second opinion. Returns None on ANY failure
    (server down, bad reply, timeout) - this is an optional, informational
    addition to the report, never something the pipeline depends on.
    """
    try:
        content = colibri_client.chat(
            build_prompt(metrics, warnings=warnings, score=score, rule_passed=rule_passed),
            base_url=base_url, model=model, timeout=timeout, _post_fn=_post_fn)
    except Exception as exc:
        print(f"KI-Zweitmeinung nicht verfügbar: {exc}", file=sys.stderr)
        return None
    result = parse_response(content)
    if result is None:
        print(f"KI-Zweitmeinung: Antwort außerhalb des Vertrags, verworfen: {content!r}",
              file=sys.stderr)
    return result
