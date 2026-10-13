"""Shared local HTTP client for a Colibri server (`coli serve`).

Colibri ([JustVugg/colibri](https://github.com/JustVugg/colibri)) speaks
the OpenAI `/v1/chat/completions` API, so a plain stdlib client is enough -
see docs/AI_ADAPTER.md for why this engine was chosen. `ai_profile.py` and
`ai_quality.py` both use this module instead of each opening its own
connection, so the transport (reachability check, POST, error handling) is
written once.
"""

import json
import urllib.error
import urllib.request

DEFAULT_BASE_URL = "http://127.0.0.1:8080"
DEFAULT_TIMEOUT_S = 5.0


def available(base_url=None, timeout=1.5):
    """True if a Colibri-compatible server answers at base_url. Never
    raises - an unreachable local server is the expected default state,
    not an error the caller needs to handle."""
    base_url = base_url or DEFAULT_BASE_URL
    try:
        req = urllib.request.Request(base_url.rstrip("/") + "/v1/models")
        with urllib.request.urlopen(req, timeout=timeout):
            return True
    except (urllib.error.URLError, OSError, ValueError):
        return False


def _default_post(base_url, payload, timeout):
    req = urllib.request.Request(
        base_url.rstrip("/") + "/v1/chat/completions",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def chat(messages, base_url=None, model=None, timeout=DEFAULT_TIMEOUT_S, temperature=0.0,
         _post_fn=None):
    """Sends one chat-completion request, returns the reply text.

    Raises on any failure (unlike available()): callers here already
    decided to make the call and are equipped to turn a raised exception
    into "no suggestion" - see ai_profile.suggest_profile /
    ai_quality.suggest_quality for that pattern, and their tests for how
    _post_fn replaces the network call.
    """
    base_url = base_url or DEFAULT_BASE_URL
    post_fn = _post_fn or (lambda payload: _default_post(base_url, payload, timeout))
    payload = {
        "model": model or "default",
        "messages": messages,
        "temperature": temperature,
    }
    response = post_fn(payload)
    return response["choices"][0]["message"]["content"]
