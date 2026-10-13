# Contributing

This guide explains how multiple contributors can work on SamNPlayer
without blocking one another. Further detail lives in `HANDOFF.md`
(what the project is and why it is designed this way) and
`WIEDERAUFNAHME.md` (setting up the environment).

## Which document is authoritative?

- `README.md`: installation, usage, and getting started.
- `HANDOFF.md`: architecture, measured results, limitations, and rejected approaches.
- `WIEDERAUFNAHME.md`: restoring the environment, building, and verification.
- `docs/NEXT.md`: the single operational task list, with priorities and acceptance criteria.

Before starting work, check the current code, `git status`, `docs/NEXT.md`,
and the relevant PR. Old chat logs provide context; verify their bug
reports, permission claims, and completion claims against the current
state. When sources disagree, rely on code and reproducible tests. Do not
present values without hardware evidence as verified device properties.

Write project documentation and **user-facing UI** in English (see
`docs/LANGUAGE.md`). Preserve code identifiers, CLI flags, file paths, and
exact UI labels when they are needed to identify an existing control.
Do not rewrite Go/Python/JS stacks for fashion — only when a measured win
justifies the cost.

## Areas that can be worked on independently

The boundaries allow contributors to avoid editing the same files.
Keep changes within the agreed area unless the task requires crossing a
boundary:

| Area | Responsibility | Scope |
|---|---|---|
| `device/` | BLE protocol, Intiface, device control | Device layer |
| `generator/*.py` | Analysis, signal processing, quality assessment | Python |
| `cmd/gui-wails/frontend/` | User interface | JS/CSS |
| `player/` | Playback, synchronization, training | Go core |
| `motionx/`, `videox/` | Standalone helper packages | Package internals |

**New analysis methods do not require pipeline edits.** Use the backend
registry in `generator/backends.py`: write a function and register it with
`register()`. Contributors can implement different methods without
modifying the same file. The contract is documented at the top of
`backends.py`.

## After cloning

`cmd/gui-wails` embeds `frontend/dist` using `go:embed`. This build artifact
is not tracked in the repository, so a fresh clone initially reports:

```text
pattern all:frontend/dist: no matching files found
```

Build the frontend first:

```bash
cd cmd/gui-wails/frontend && npm install && npm run build && cd ../../..
go build ./...
```

After that, normal development can proceed. `wails build` also performs
this frontend build step.

## Workflow

1. Record the objective and acceptance criteria in `docs/NEXT.md` or a linked
   issue. Take on one manageable task and identify the affected areas.
2. Fetch the current `main` and create a branch, for example
   `codex/project-workflow-cleanup` for Codex work. Check local changes first;
   do not overwrite or reset another contributor's work.
3. Implement the change and run appropriate tests. Behavior changes need a
   regression test with a negative check. Documentation-only changes do not
   need artificial tests; verify facts, links, and commands instead.
4. Open a pull request describing the problem, result, checks performed,
   and remaining limitations. Never publish empty files or placeholders as
   an intermediate solution. Review the complete diff before committing.
5. Wait for CI and obtain a review. Changes to `device/` should include a
   hardware test; clearly identify mock-only testing.
6. **Before every release tag:** (a) bugfix the tip (leftover user-facing
   German, broken seeks/handlers, missing bindings), and (b) re-run the
   clip7776 playback suite
   (`python3 cmd/gui-wails/frontend/test/pb_clip7776_test.py` plus related
   `pb_*` / curve / playlist tests). Do not tag if either step fails or
   shows a regression versus `main`.
7. After merging, reconcile `docs/NEXT.md`. Reference a PR or commit for
   completed work and record architectural findings in `HANDOFF.md`.
   Delete old branches only after verifying that their changes are integrated.

## What a contribution must include

**For behavior changes, a test that fails without the change.** Twice in
this project a feature was implemented but did nothing: once an insertion
landed in the wrong file, and once a parameter had no effect anywhere.
Measurements exposed both problems. Write the test, see it pass, temporarily
revert the change, **see it fail**, then restore the change.

**Measurements instead of impressions.** Support improvement claims with
numbers in a comment or test. Plausible explanations have repeatedly turned
out to be wrong: camera compensation was not broken, the test material was;
smoothing did not destroy amplitude; an additional estimator made the
result worse.

**Document negative results too.** Record rejected ideas and measurements
in the “Tested and rejected” section of `HANDOFF.md`, so someone does not
repeat the same experiment three months later.

**Do not add unused functionality.** Two modules currently remain unused
in the tree (`fusion.py`, `videox/`), each with a documented reason for not
being connected. This is an exception, not the intended pattern.

## Pitfalls that have cost time

- **Noise-background test videos** are unsuitable for camera compensation:
  `goodFeaturesToTrack` cannot find stable features and the estimate becomes
  random. Always use textured backgrounds.
- **Moving objects in test videos** must use world coordinates so they
  follow camera pans; otherwise compensation appears incorrectly broken.
- **Increment `TRACK_CACHE_VERSION`** when changing `track_roi` or camera
  compensation, or the cache silently returns results from the old version.
- **Run `wails build` after adding a Go method**, or missing bindings will
  cause misleading frontend-test failures.
- **Use `-trimpath` when building.** Otherwise the build machine's path,
  including the username, ends up in the executable.

## Third-party code

Check the license before reusing anything from another project. The table
in `HANDOFF.md` records the project's assessment. In particular, the project
excludes FunGen source-code reuse and structurally equivalent ports because
of its PolyForm Strict licensing and the intended commercial use of
SamNPlayer. Publicly described behavior may inform independent designs.

## Versions

`VERSION` and `update.BaseVersion` must contain the same version without a
`v` prefix. `go test ./update` checks this. Development binaries display
`BaseVersion-dev`; release binaries receive the tag version through `-ldflags`.

Before a new release:

1. Update both version values in the same PR, revise relevant documentation
   and `docs/NEXT.md`, and complete CI successfully.
2. Tag the verified commit on `main` with `vX.Y.Z` and push that specific tag.
3. Before building, the release workflow checks the tag, `VERSION`, and
   `BaseVersion`. It stops on a mismatch. Go and Wails CLI versions come from
   `go.mod`.
4. Verify the workflow result, four binaries (GUI/CLI for Windows/Linux),
   and `checksums.txt`. A successful build does not replace a hardware test
   or a self-update test using the installed application.

Do not move published tags. Correcting the source version does not modify
an already published release.
