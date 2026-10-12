# SamNPlayer Next Level

Next Level is the future independent software generation derived from SamNPlayer. **Design and research phase — no production release yet.** The current Free/Community application continues separately.

## Architecture and development

- [Full multi-year engineering roadmap](docs/NEXT_LEVEL_MASTER_ROADMAP.md): starting from existing Go/Wails/FFmpeg/OpenCV and classical script generator, automatic per-scene tracking/quality verification, benchmark-gated Go/Rust native improvements, device compiler, AI-assisted refinement, full professional multi-axis editing, batch and release gates.
- [Professional multi-axis editor specification and competitor research](docs/EDITOR_MULTIAXIS_AND_COMPETITIVE_BLUEPRINT.md): editing tools, axis lanes, transaction-safe undo, interchange formats, test criteria and dependencies.

The central idea: **video + target → autonomous classical first-pass → per-scene candidate selection → optional locally assisted AI → independent verification → reliable Funscript/project → safe device-aware playback.**

## Product editions

**SamNPlayer Free (Community):** excellent, approachable basic Create/Play/Edit/Training and device safety, donation-supported. Future simplified Free focuses on fewer AI capabilities and no permanent batch. Selected new experimental features may get a **seven-day Preview** within the existing Free app (implementation work lives in the original SamNPlayer repository).

**SamNPlayer Suite:** internal extended build and evaluation tools in the original repository.

**SamNPlayer Next Level (2.0):** the next major software generation, built from the existing SamNPlayer codebase and Flow layout, with adaptive native motion engine, full multi-axis editor, AI-assisted improvement and recoverable batch. **Freeware supported by voluntary donations, not a paid Studio licence.** No paid unlock, paywall, €40 entitlement or mandatory subscription is currently planned.

**Latest owner decision (12 October 2026):** the previously discussed commercial Studio/tier/licence/supporter-pass model is superseded. Next Level is a real **SamNPlayer 2.0**, released when ready under a donation-supported freeware model. Free v1 continues separately. Existing code and the established Flow GUI should be reused, not replaced solely for the sake of a redesign.

## Development & contribution rules

- Build the core and quality gates before risky architectural replacements.
- Preserve `.funscript` interoperability and backward-compatible `.samn` migration.
- Keep user videos, learning data, secrets and model weights out of this public repository.
- No large dependency or AI model without license and maintainability review.
- Public roadmap does **not** mean existing proprietary SamNPlayer source history is publicly licensed or approved for import. Code migration requires explicit confirmation and audit.

## Current status

This repository is public and currently contains planning documents. The existing source code has **not yet been migrated** here. No claims of working Next Level engine or release should be inferred from the roadmap.
