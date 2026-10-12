#!/usr/bin/env python3
"""ai_profile.py - suggests a generator profile (standard/weich/tf/tj) from
a video's motion signature, as a fallback AI judgment when the CLASSICAL
tool (`motion_signature.find_similar`) finds no confident match.

This is the second step of docs/AI_ADAPTER.md's plan (region -> profile ->
quality). It deliberately builds ON TOP of the existing non-AI tool instead
of duplicating it - exactly the "AI uses the results/tools of the non-AI
system, they work together" requirement the plan was written for:

  1. `motion_signature.extract()` measures the scene (unchanged, no AI).
  2. `motion_signature.find_similar()` compares it against user-named
     examples (unchanged, no AI). Below its distance threshold this ALONE
     decides the suggestion - ai_profile.py is not even imported.
  3. Only when step 2 is inconclusive (no known example close enough) does
     this module ask a local LLM to reason over the same signature numbers
     and the nearest known examples, and return a suggestion WITH a reason
     in prose.

The suggestion is always informational. Nothing here writes to
`--profile` automatically - docs/NEXT.md is explicit that profile
switches need validation, not just a plausible-looking number.

WHY NO TRAINED MODEL: shipping a trained classifier here would repeat the
mistake quality_model.py explicitly guards against - a model trained on
too little labelled data looks confident while being wrong, and unlike
quality_model.py there is currently no labelled corpus at all (nobody has
named enough scenes yet, see motion_signature.save_labelled). A zero-shot
LLM judgment needs no training data and can say "unsure" outright, so it
is the honest option until enough labelled examples exist to train and
cross-validate a real classifier the way quality_model.py does.

TRANSPORT: `colibri_client.py` talks to Colibri's `coli serve` (OpenAI
`/v1/chat/completions`, see docs/AI_ADAPTER.md) - a plain stdlib HTTP
client, no new pip dependency, matching requirements-ai.txt staying
minimal. `ai_quality.py` (step 3) shares the same client.
"""

import json
import sys

import colibri_client

# "tj" used to be a fourth choice here, identical to "tf" in every respect
# (funscript/recipe.go's NormalizeProfile always collapsed both to the same
# recipe) - dropped so the model is never asked to pick between two options
# that mean the same thing. generate_funscript.py's --profile still accepts
# "tj" for old scripts/commands; this suggestion contract just never offers
# it as a fresh choice anymore.
KNOWN_PROFILES = ("standard", "weich", "tf")

_SYSTEM_PROMPT = (
    "You classify a short video scene's motion signature into one of a "
    "fixed set of device profiles for a funscript generator. You are not "
    "shown the video, only eight measured numbers and, optionally, a few "
    "previously named examples with their distance to the current scene. "
    "Never invent a profile name. Reply with ONLY a single JSON object: "
    '{"profile": one of ' + json.dumps(list(KNOWN_PROFILES) + ["unsure"]) +
    ', "confidence": a number from 0 to 1, "reason": one short sentence}. '
    "Use \"unsure\" whenever the signature does not clearly resemble a "
    "known example - a wrong guess is worse than admitting uncertainty."
)


def build_prompt(signature, known_examples=None):
    """Pure function: signature + nearby named examples -> chat messages.

    Testable without a network call. known_examples is the same shape
    motion_signature.find_similar() consumes: a list of
    {"label": str, "signature": dict}, here already limited to the few
    nearest ones (see suggest_profile()) so the prompt stays short.
    """
    import motion_signature

    lines = ["Current scene signature:"]
    for field in motion_signature.SIGNATURE_FIELDS:
        lines.append(f"  {field} = {float(signature.get(field, 0.0)):.3f}")

    if known_examples:
        lines.append("\nNearest previously named examples:")
        for entry in known_examples:
            d = motion_signature.distance(signature, entry.get("signature", {}))
            lines.append(f"  \"{entry.get('label')}\" (distance {d:.3f})")
    else:
        lines.append("\nNo previously named examples are available yet.")

    return [
        {"role": "system", "content": _SYSTEM_PROMPT},
        {"role": "user", "content": "\n".join(lines)},
    ]


def parse_response(raw_text):
    """Pure function: raw model text -> {"profile", "confidence", "reason"}
    or None if the model did not return a well-formed, in-contract answer.

    Deliberately strict: a malformed or out-of-contract reply is treated
    the same as no answer, not coerced into something usable - a silently
    "fixed" bad answer would look like a real suggestion.
    """
    try:
        data = json.loads(raw_text)
    except (json.JSONDecodeError, TypeError):
        return None
    if not isinstance(data, dict):
        return None
    profile = data.get("profile")
    if profile not in KNOWN_PROFILES and profile != "unsure":
        return None
    try:
        confidence = float(data.get("confidence", 0.0))
    except (TypeError, ValueError):
        confidence = 0.0
    reason = str(data.get("reason", "")).strip()
    return {"profile": profile, "confidence": max(0.0, min(1.0, confidence)), "reason": reason}


def available(base_url=None, timeout=1.5):
    """Re-exported for convenience so callers only need `import ai_profile`.
    See colibri_client.available() for the actual behavior."""
    return colibri_client.available(base_url=base_url, timeout=timeout)


def suggest_profile(signature, known_examples=None, base_url=None, model=None,
                     timeout=colibri_client.DEFAULT_TIMEOUT_S, _post_fn=None):
    """Ask the local LLM for a profile suggestion. Returns None on ANY
    failure (server down, bad reply, timeout) - this is an optional,
    informational fallback, never something the pipeline depends on.
    """
    try:
        content = colibri_client.chat(
            build_prompt(signature, known_examples),
            base_url=base_url, model=model, timeout=timeout, _post_fn=_post_fn)
    except Exception as exc:
        print(f"KI-Profilvorschlag nicht verfügbar: {exc}", file=sys.stderr)
        return None
    result = parse_response(content)
    if result is None:
        print(f"KI-Profilvorschlag: Antwort außerhalb des Vertrags, verworfen: {content!r}",
              file=sys.stderr)
    return result
