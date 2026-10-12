# Colibri setup — local LLM for profile / quality opinions

SamNPlayer talks to a **local OpenAI-compatible server** for optional prose
help: **Suggest profile** (fallback when no similar saved scene) and the
Create **AI quality second opinion**. Everyday Create stroke writing stays
CSRT. Nothing is uploaded; nothing is required to use the app.

Upstream: [JustVugg/colibri](https://github.com/JustVugg/colibri) (`coli serve`).
Any server that answers `GET /v1/models` and `POST /v1/chat/completions` works
(LM Studio, Ollama with OpenAI bridge, etc.). Architecture: `docs/AI_ADAPTER.md`.
Owner overview of all local models: `docs/LOCAL_MODEL_SETUP.md`.

## Colibri as a vision teacher (contact points)

Colibri can serve very large MoE models, streaming the experts from NVMe.
Two of its families see images: GLM-5.3-Flash and DeepSeek V4.1 Flash. That
makes them usable as slow but strong **keyframe teachers**:

```bash
python3 generator/contact_points.py --video clip.mp4 --teacher colibri:<model id>
```

- The preset URL is `http://127.0.0.1:8000`, which the upstream README
  documents as the default, with a 30-minute timeout per request.
- The prose helpers below use the Settings URL (default 8080). Run the port
  you configure there.

See `docs/VLM_MODELS.md`.

## Quick start (Colibri)

1. Build/install Colibri from upstream and load a small chat model (Qwen or
   similar — the app sends `model: "default"`; map that in your server if needed).
2. Start the server, typically:
   ```bash
   coli serve
   ```
   Default listen address used by SamNPlayer: **`http://127.0.0.1:8080`**.
3. In the Emotion GUI → **Settings** → **AI server for profile suggestion &
   quality second opinion**:
   - Leave the field **empty** to use `http://127.0.0.1:8080`, or paste another
     base URL (no trailing path; do not append `/v1/...`).
   - Use **Test AI server** — it probes `GET /v1/models`. Unreachable is
     normal when nothing is running; Create still works.
4. Create tab:
   - **Suggest profile** — classical / Go model first; Colibri only if those
     have no safe answer.
   - Optional checkbox for a quality second opinion — never changes
     `quality.passed` / score; opinion only.

CLI equivalents: `--ai-base-url`, `--suggest-profile`, `--ai-quality-opinion`
in `generator/generate_funscript.py`.

## What it does *not* do

| Not Colibri’s job | Where that lives |
|-------------------|------------------|
| Tip / body-part boxes | ONNX ROI (`docs/KI_TRAINING.md`) |
| Stroke curve / Everyday Create | Go CSRT |
| AI draft script | Imitation library today (`docs/AI_SCRIPT_WRITER.md`); ONNX writer later |
| Bundled weights / auto-download | None — by design |

## Troubleshooting

| Symptom | Try |
|---------|-----|
| AI server check → not available | Is `coli serve` (or LM Studio / Ollama bridge) listening? Firewall? Wrong port in Settings? |
| Suggest profile ignores AI | A saved scene or Go profile model already matched — Colibri is fallback only |
| Quality opinion missing | Enable the Create checkbox; confirm the AI server check is green |
| Timeout / hang | Large models on CPU are slow; keep fail-closed timeouts |

## Related

- `generator/colibri_client.py` — shared HTTP client (`DEFAULT_BASE_URL`)
- `generator/ai_profile.py` / `ai_quality.py` — prompt + parse
- Settings key: `generator.aiBaseUrl` (empty = default localhost)
