> **Archived input** — copied unchanged from `funfunpayer/SamNPlayer` PR #510 (head `121acd7`, 12 Oct 2026, not merged).
> Superseded where it disagrees with [`docs/MASTER_PLAN.md`](../MASTER_PLAN.md). Do not edit; change the master plan instead.

# Owner decision: Free quality, Next Level innovation (2026-10-12)

**This decision supersedes the earlier proposal to build large Studio/AI machinery inside the original Free repository.**

## SamNPlayer Free: high-quality, focused core edition (later scope reduction approved by Owner)

- Keep polished Play, Create, Edit, Training, tools, current classical generator, current working optional AI helper features, existing script formats, reliable device controls and emergency stop.
- Prioritize measured quality, usability, performance, compatibility, documentation, security, fast startup and successful Windows releases.
- Do NOT substantially expand heavyweight AI training, automatic AI-script-writing research, experimental Go/Rust generator rewrites or professional adaptive multi-backend processing here.
- **Owner correction:** in a LATER clearly communicated edition split, deliberately simplify Free to the essential polished workflow. Free gets substantially **less AI than the current development build**, and **batch/queue processing is removed from the future Free product** and reserved for Next Level Studio. This is intentional product focus, not an artificial slowdown or quality degradation. The current near-term Free build stays intact until an explicit transition is approved.
- Before removing bundled training Python, runtime dependencies or a user-facing control, audit actual call sites and edition builds, prove no-loss of existing Free behavior with automated and native QA, and seek Owner approval for a breaking change.
- **Migration requirement:** inventory existing batch UI, API, scheduler, tests and AI training/draft/model features; in the future focused Free edition cleanly remove/hide batch entry points and unnecessary packaged components while preserving single-video Create/Save/Play/Edit, fundamental validation, stable classical tracking, useful core capabilities and device safety. Do not merely hide CSS controls while leaving unneeded heavy background tasks enabled. Scope any removals by a tested edition feature manifest; no cleanup before current Free release QA. Publish honest changelog and forward compatibility. Respect existing license grants and third-party rights.

## SamNPlayer Next Level: new separate software generation derived from Free

- Private proposed repo: funfunpayer/SamNPlayer-Next-Level (not yet created via current connector). Inherit the existing program/code/history after a safe private migration.
- Own: adaptive classical-first full automation; per-scene backend selection and verified re-run; local model/LM Studio and optional AI draft/repair; opt-in learning and curated correction datasets; profiled native Go/Rust/C++ core; full professional multi-axis editor; reliable batch pipeline and per-device optimized output.
- Always independently verify generated or AI-improved curves against actual video evidence, timestamp correctness, quality metrics and device limits. Do not present guessed confidence as proof.
- Keep a universal Funscript export and backward-compatible .samn handling. New axes require real evidence or explicit user scripting; no fabricated device channels.
- Build comprehensive synthetic/golden tests and Windows 11 native QA, Mac LM Studio integration tests, device mocks, safe physical testing, performance benchmarks and local-only privacy checks.
- The long-term goal is demonstrably superior reliability, usability and output quality against public competitor baselines; never claim a comparative win without fair identical-media/hardware benchmarks.

## Cross-project workflow

1. Finish existing Free GUI integration (#509), #489 visual polish, regression/Windows QA, documentation, public website and release.
2. Create and privately bootstrap Next Level with full safe code ancestry (see NEXT_LEVEL_REPOSITORY_BOOTSTRAP_2026-10-12.md).
3. Develop Next Level experimentally, in isolation from stable Free releases. Advance by golden-clip and device-safety gates even when research takes months/years.
4. Upstream or cherry-pick stable bug fixes deliberately with tests. No automatic bidirectional merging and no second experimental stack in Free.
5. Free receives reliability/security and small compatible innovations; Next Level takes heavyweight new generation, Editor and Studio commercialization. **After Free release and with explicit Owner go-ahead, ship the focused Free edition: reduced AI and no batch processing; regression-test the single-file workflows and migration from prior Free releases.**

## Ownership and launch

- Claude: Go/native core, generator rewrites only in Next Level, wiring, tests and release.
- ChatGPT: visual implementation, copy, documentation and benchmark/architecture proposals on independent branches.
- Owner: release, paid Studio and user-facing changes; no new repository or payment collection claimed as already complete.
- Guaranteed Studio access for support above EUR 40 requires explicit purchase/entitlement terms and legal/payment review; current Free donations do not grant a licence under the present EULA.

## Deferred Free simplification acceptance gates

1. Baseline inventory: list every shipped AI-related control, underlying API/runtime process, model installer, data-store dependency and batch queue path; distinguish basic optional ROI helper from Studio training/writer features.
2. Decide a concrete Free feature manifest with Owner sign-off, rather than broad claims; keep standalone functional Play/Create/Edit/Training, standard Funscript and existing essential device/safety controls.
3. Disable Studio-only paths at the Go/backend boundary; avoid accidental calls to removed Python runtimes and nonexistent batch workers. Validate startup and portable package without removed assets.
4. Migrate old single-job project and script libraries non-destructively; preserve existing saved batch project files as user data even if the new Free edition cannot run batch (warn/open in Studio or offer single-item import when possible).
5. Verify Free regression suite: one video analysis to script, playback sync, save/export/import, no-model fallback, undo/redo, all current device safety caps and emergency stop.
6. Mark changed Free functionality clearly in release notes and public website, including editions/upgrade path. No silent update that destroys an existing project.
7. Do **not** perform the simplification now. Focus on finishing and publishing the current Free release first; Next Level development in separate repository.


## Final Owner positioning: Community excellent, Next Level exceptional (2026-10-12)

**Community Edition / Free** must be **excellent at the essentials**, polished, dependable and worth recommending. It does **not** need Studio-grade depth or best-in-class professional automation. No deliberately broken, unsafe or artificially slowed-down workflows. The Owner's distinction is *good/excellent and focused*, versus *exceptional professional Next Level*, not parity of feature breadth.

- Always maintain **Donate / Support Development** in Community Edition, in app and on website. Voluntary donations fund community bug fixes and future exploration. Current EULA §6: donations without guaranteed reward. A later guaranteed Studio entitlement for contributions over EUR 40 requires separate binding terms/payment classification before advertising or accepting.
- **Stable Community features:** reliable single-video classical generation, normal video-to-script workflow, functional standard Editor/Player/Training, supported devices and emergency stop, standard import/export. Feature matrix subject to an Owner-reviewed audit, but user data cannot silently disappear.
- **Reduced stable AI:** keep only a modest, useful Community AI scope later; most advanced model-training, generative repair, autonomous selection and deep local learning move to Next Level.
- **No permanent Batch in future Community:** professional batch processing belongs to Next Level. The current Free release being finished is not retroactively stripped; schedule controlled transition with full compatibility and truthful release notes.
- **Time-limited Studio feature previews in Community:** for selected new features, explicitly advertise start/end dates, quality/experimental state, hardware/model prerequisites, and that access expires or is withdrawn after the announced trial window. Preview is not a permanent promise. May offer periodic Preview Weeks or fixed release-based windows; never pretend an unannounced expiry is a bug.
- **No destructive expiry:** expiration disables only the premium action in Community. Existing user-authored video, .funscript, .samn and projects remain readable/exportable where technically possible. Safe fallback: standard Free viewer/export or a documented conversion path. Playback/emergency stop must never be gated by trial expiry.
- **Preview release contract:** feature manifest records id, label, edition, enabled_from_utc, enabled_until_utc, platform, model_requirements, fallback_action, migration_behavior. Expiry should be enforced by backend product/edition logic, not CSS hiding alone. Offline behaviour must be documented (time source tampering / no network) without a forced cloud dependency.
- **Feedback loop:** opt-in feedback from Preview testers (manual reports, no automatic intimate-video uploads), scene repro and quality benchmarks; classify each preview after testing as `Community Stable`, `Studio Only`, `Redesign` or `Withdraw`. Keep a public changelog of decisions.
- **Marketing:** Community Edition = "Free. Reliable. Community-supported." Next Level / Studio = "Professional automation, full multi-axis editing, advanced hybrid AI and batch". Avoid false product performance or superiority claims.

**Implementation order:** finish current Free, document future focused Community edition, finish Next Level core and preview-gating design, then run the first explicitly advertised limited-time Community feature preview **only when working Studio code and expiry/migration tests exist**. No immediate expiring trial implementation in the unfinished Free release.
