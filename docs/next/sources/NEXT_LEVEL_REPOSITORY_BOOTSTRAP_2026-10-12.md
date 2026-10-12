> **Archived input** — copied unchanged from `funfunpayer/SamNPlayer` PR #510 (head `121acd7`, 12 Oct 2026, not merged).
> Superseded where it disagrees with [`docs/MASTER_PLAN.md`](../MASTER_PLAN.md). Do not edit; change the master plan instead.

# SamNPlayer Next Level — repository bootstrap and migration contract

Status: **prepared, not created**. Owner authorizes an independent new software generation using the existing private SamNPlayer codebase and full history. This is NOT a replacement of Free; current Free remains in its own repository and release line.

## GitHub repository requested

- Repository: `funfunpayer/SamNPlayer-Next-Level`
- **Private**
- Description: `Next-generation SamNPlayer Studio — adaptive native motion engine, verified local AI, full multi-axis editing and automated QA.`
- Do not initialize README/license/gitignore in GitHub creation form: migration transfers existing complete Git history.
- Access: owner + Claude + ChatGPT GitHub connector after explicit connector authorization. Avoid exposing source/models/benchmarks.
- Default branch `main`; branch policy via GitHub GUI if permitted: PR reviews, successful CI, no direct push, secret scanning and Dependabot as appropriate.
- **Do not fork publicly or copy the repository into any public destination.**

## Exact full-history bootstrap (owner-controlled, once repo exists)

On the trusted development machine with rights to BOTH private repositories:

```sh
git clone --origin origin-free git@github.com:funfunpayer/SamNPlayer.git SamNPlayer-Next-Level
cd SamNPlayer-Next-Level
git switch main
git remote add origin git@github.com:funfunpayer/SamNPlayer-Next-Level.git
git push -u origin main
# Preserve existing history/tags in a separate archival namespace, not as public releases:
git push origin 'refs/remotes/origin-free/*:refs/heads/legacy-free/*' # OPTIONAL, usually omit
```

**Better minimal default:** push only `main` while preserving its full ancestor history; do **not** push all old Free feature branches, secret-bearing historical refs, workflow artifacts or tags blindly. Audit git history and secret scanning before first push; history contains everything reachable from main. Existing historic MIT grants remain; current proprietary licence applies only where legally valid.

After bootstrap:
```sh
git switch -c next/architecture-bootstrap
# Add docs and CI from this concept as a reviewed PR in Next Level
git push -u origin next/architecture-bootstrap
```

Avoid `git push --mirror`, which would blindly copy refs, old PR development branches/tags and sensitive artifacts. If SHA history must be preserved exactly, do not rewrite it; if secrets exist, pause migration for an explicit owner-approved remediation.

## Architecture and top-level ownership

```text
SamNPlayer-Next-Level/
  cmd/                   # inherited Wails application; evolve product as one app
  generator/             # inherited Python/OpenCV generator; baseline until proven parity
  device/                # inherited device/protocol integration
  sam/                   # inherited internal motion representation
  docs/
    NEXT_LEVEL_MASTER_PLAN.md
    ARCHITECTURE.md
    EDITOR_MULTIAXIS.md
    AI_LEARNING.md
    DEVICE_MATRIX.md
    COMMERCIAL.md
    QA_RELEASE_GATES.md
  research/
    BENCHMARK_METHODS.md
    THIRD_PARTY_LICENSE_AUDIT.md
    COMPETITORS.md
  prototypes/
    go/README.md
    rust/README.md
  benchmarks/
    README.md
    datasets/README.md  # manifests only; NO actual sensitive videos
    results/.gitkeep     # reports anonymized by default
  testing/
    windows11/README.md
    lm-studio-mac/README.md
    integration/README.md
  .github/workflows/
    # existing CI initially, extend after migration only
```

Empty folders are not useful: every README must contain a clear runnable experiment, expected artifact and pass/fail exit code before feature work begins.

## Free <-> Next Level collaboration contract

1. Free fixes developed/tested in `SamNPlayer`; port/cherry-pick appropriate bugfixes to Next Level with tests and provenance. Avoid automatic bidirectional merges once architectures diverge.
2. Next Level owns Studio automation, adaptive generation, full professional multi-axis editor, experimental Go/Rust implementations, training/benchmarks. No unreviewed changes to Free based on experiments.
3. Shared exported `.funscript` remains interoperable, `.samn` is backward-compatibly extended. Add serialization compatibility tests.
4. Free remains published from old repo/site. Next Level is private research and separate later Studio release until product/legal gates passed.
5. One independent release and updater channel per edition; prevent Studio/Free accidental cross-edition updates.
6. Explicit owner permission before importing proprietary third-party code or redistributing model weights. Use OpenFunscripter only as UX reference; MIT projects require notice if copied.

## Windows 11 / Mac LM Studio integration plan

### Configuration

Windows 11 is the **performance and system integration test machine**. Mac hosts an optional **LM Studio local vision-language inference server**. Do not put credentials or private video content into repo. Configure endpoint using e.g. `SAMN_LM_STUDIO_URL=http://<mac-lan-ip>:1234/v1` (placeholder; require user to configure Mac IP). Both machines on private trusted network; firewall restrict to Windows host; never expose unauthenticated server to public internet.

### Data flow

```text
Win11 Wails Next Level -> Go Orchestrator -> local classical analysis/Tracking
                             |
                             +-> optional Mac LAN LM Studio VLM for ROI/scene/draft
                             |       (only user-enabled, only selected resized frames)
                             +-> independent verifier on Windows
                             +-> quality + timing + device-safe master curve
```

The user requested locally learning AI: LM Studio is **inference serving**, not by itself a continuous safe learning/training pipeline. Model feedback/curated training should be separate, opt-in, versioned, benchmarked and rollback-capable. Never send full intimate videos or frames to another host without user opt-in—even on a home network.

### Test levels

- **L0 static**: Go tests, Python legacy tests, frontend tests, Rust `cargo test/clippy/fmt` once Rust exists, schema and licence notice checks.
- **L1 deterministic fixtures**: synthetic frames, known timebases/VFR mappings, deterministic ROI tracking, 0..100 curves, axis serialization, cancel/resume.
- **L2 golden media**: owner-supplied lawful local clips; labels and sensitive files stored outside git, sanitized manifests only. Compare wall time/RAM/frame throughput, lost frames, time alignment, action error/windowed correlation and quality report.
- **L3 local AI**: mocked OpenAI-compatible VLM server + optional real LM Studio smoke on LAN, calibration for coordinates and schema. Timeout, no connection, invalid JSON, hallucinated ROI, model unavailable -> deterministic fallback.
- **L4 hardware mock**: capability matrices (Neo2, Handy, multi-axis, vibration/suction, no device) with hard caps and independent emergency-stop simulator.
- **L5 manual Win11 native Wails**: 1366×768 + 1920×1080, long clips, USB/BLE issues, restart, unplug, device commands supervised at safe level.
- **L6 performance bake-off**: pair identical fixtures and hardware with reproducible env, three warm and five measured runs, median + p95, peak RAM, CPU/GPU use, quality non-regression; no Rust rewrite without measured benefit.
- **L7 international Studio release**: purchase/refund/entitlements only after reviewed legal/processor setup, offline licence verification, docs and privacy checks.

## Initial 90-day work order (not deadlines)

**Gate N0**: inventory reproducible Free main baseline and working tests, PR #509 visual integration and release requirements from Free.
**Gate N1**: current code imported with full history; Next Level CI green, no secret leaks, full .samn/funscript format round-trip.
**Gate N2**: native timestamps, shared jobs/candidate contract, Quality Doctor baseline run.
**Gate N3**: multi-axis editor architecture + binary-compatible importer/exporter + undo transaction tests.
**Gate N4**: auto methods, selective retry, AI optional verifier and batch recovery.
**Gate N5**: device adapters and capability mock profiles, Win11 hardware smoke.
**Gate N6**: competitive comparative benchmarks, commercial/legal Studio go/no-go.

May take months/years; advance only at each measurable gate. The Free app continues on its own cadence.

## Concrete user action needed

Create the **private empty** repository via GitHub UI: `https://github.com/new` -> Owner `funfunpayer` -> name `SamNPlayer-Next-Level` -> Private -> no initialization files -> Create repository. Confirm the linked GitHub connector has repository access (GitHub App installations may need access granted for a new repo). Then the full-history migration command above must run on a trusted machine with Git credentials. Our connector can add files/PRs AFTER the repository becomes visible but cannot itself create the repository or perform cross-repository git push/mirror.

## Related plan

Detailed Owner vision, product feature matrix, monetization: `docs/FREE_TO_STUDIO_PRODUCT_PLAN_2026-10-12.md`. Multi-axis editor and comparative research: `docs/STUDIO_EDITOR_COMPETITIVE_BLUEPRINT_2026-10-12.md`. These must be copied to the new repo on bootstrap **after review**, not deleted from the Free repository before migration.
