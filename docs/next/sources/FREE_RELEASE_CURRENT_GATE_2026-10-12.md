> **Archived input** — copied unchanged from `funfunpayer/SamNPlayer` PR #510 (head `121acd7`, 12 Oct 2026, not merged).
> Superseded where it disagrees with [`docs/MASTER_PLAN.md`](../MASTER_PLAN.md). Do not edit; change the master plan instead.

# Current owner-approved product names and Free completion gate (12 October 2026)

**Document purpose:** prevent the older Free→Studio design documents from conflating the current test edition with the future commercial product. This is a planning note, not a release.

## Names

- **SamNPlayer Free (Community)**: public, donation-supported app; initially finish and release stable, useful single-video Create/Edit/Play/Training with polished GUI; later deliberately scope down heavyweight AI and batch to core functions with documented non-destructive migration.
- **SamNPlayer Suite**: internal extended testing/development edition, currently in the main repository. Includes AI training and batch test features; separate from future paid Studio. PR #513 proposes renaming existing 'studio' build tag to 'suite'; merge/QA pending.
- **SamNPlayer Next / Studio**: future independent commercial software generation, based on existing code/history, intended private new repository `funfunpayer/SamNPlayer-Next-Level`; **not yet created**. This replaces earlier vague wording that called current developer Suite binaries 'Studio'.
- Donation feature in Free stays; selected Next features may later run time-limited Community previews with explicit notices and non-destructive expiry. Guaranteed Studio access for qualified support >40 EUR requires revised commercial terms.

## Live work status checked 12 Oct 2026

- #509 (Flow C1–C4 integration) **merged** at 2026-10-12 11:29:54 UTC; integrated native screenshot QA still necessary.
- #489 Flow CSS polish **open, draft, GitHub mergeable=clean** at time checked. It is not a production approval; check against #509 current actual DOM and selected/paused/disabled/reduced-motion states, 1366×768 and 1920×1080.
- #513 edition rename **open**; verify tests/packaging/updater and complete CI before merge, especially public download must only include Free artifacts.
- #510 long-term docs concept **open**. Avoid blocking Free release on Next research.
- Full CI for latest main/#513 was running at check time. Do not infer release-ready from mergeable state or PR descriptions.

## Free release hard gates

**FREE-R1 Functional:** current main checkout, clean Go/Python/browser tests; native Windows Wails Create→funscript/.samn→Edit→Play→Save/Export; video and script selection syncing; Play/Pause/Resume/Stop; missing media/audio/model; device mock and Emergency Stop.

**FREE-R2 UI:** after #489, actual 1366×768 and 1920×1080 Windows captures for Play, Create, Edit, Training, Tools; test clip selection, focus rings, color/contrast, disabled, reduced-motion, real-time rings and empty states; match approved five-screen visual target, not mocked graphics.

**FREE-R3 Edition:** review #513 before release; Free and Suite compilation, frontend edition data attributes, denied Suite-only bindings in Free; updater edition matching; public publish script permits only Free binaries and legal notices. Report CI conclusion with SHA.

**FREE-R4 Artifact:** unsigned/signed build state honestly noted, packaging icons/assets verified (especially no `/src/assets` runtime URLs), SHA256, scan, Windows installation + portable extraction, errors clear and no false test data.

**FREE-R5 Docs/site:** README, VERSION, CHANGELOG, EULA, user handbook, releases/tags and public SamNPlayer-site actual download links confirmed; brand Free Community, Suite internal; no paid Next buy button before product/payment/legal ready. Donate CTA remains in app and site when configured.

**FREE-R6 PR cleanup:** close only genuinely superseded PRs with clear evidence/links; #509 merged takes precedence over old C1–C4 heads. Do not prematurely delete work or merge #489 solely because it is clean. Owner reviews Windows native smoke and signs off tag/public release.

## Ownership and next step

Claude: core, edition rename #513, #489 merge integration safety review, CI/native Wails and release. ChatGPT: CSS #489 focus polish, documentation corrections and public site in its own branch once verified screenshots/downloads exist. Owner: Windows hardware/real-UI acceptance and release go/no-go; creates new Next repo later.

On creation of Next repo, port the selected architecture/editor/benchmark plans from #510 as reviewed docs, preserving full source ancestry safely. Current Free work should continue today without waiting.
