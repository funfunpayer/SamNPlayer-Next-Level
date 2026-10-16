# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 16 Oct 2026, Claude, branch `claude/samn2-cpp`.

## Rules

- **Owner:** talk in German; the code, the interface and the docs are in English.
- **Branches:**
  - Claude works on `claude/*`, Grok on `grok/*`. Never push to another agent's branch.
  - Nothing reaches `main` without a PR.
  - Merge only on green CI.
  - Never force-push.
- **Before every push:** run `git diff --stat` and check it. Never write a file from a shell substitution that may be empty, and never regenerate a workflow from scratch (see `ROADMAP.md` § Lessons).
- **Decisions:** look in `DECISIONS.md` first. Never restart a decided question; record new decisions there.
- **Never:**
  - adult media in the repo or in screenshots; test patterns only;
  - model identifiers in code, docs or commit messages;
  - copied GPL, BSL or EULA code;
  - placeholders that pretend to work;
  - tests that "SKIP" with exit 0 in CI.
- **Releases:** the Owner pushes tags.

## Done

- **P0** (#6): the foundation and CI.
- **P1 Python player** (#7): the reference port, now removed. Its tests live on as the C++ tests.
- **C++ pivot** (`claude/samn2-cpp`, Owner 16 Oct: C++ first, QML, every 0.7 function, the 0.7 look, a better program):
  - **Core:** all P1 logic is in `samn_core` (funscript, `.samn`, mapper, marks, script finder, settings, playback, stroke filter, beat bar), with 50 doctest cases. `.samn` and the marks files are byte-identical with Go.
  - **QML shell:** the Flow look with the logo, light ribbons, mode pill and animated mode deck (keys 1–5, Esc, M), and the Player page with video, gradient progress, the animated Play ring, the curve (glow, level of detail, playhead) and the offset.
  - **CI:** Qt 6.7 on Linux and Windows, ctest, parity tests, and an app smoke test (start, load, render, quit).
  - **Docs:** `DECISIONS.md` (126 decisions, with the conflicts resolved), `ROADMAP.md` (0.7 feature inventory → phases, lessons) and `PLAN.md`.

## Next (P1, in order)

1. Library panel (left) and Inspector (right), as in 0.7.
2. Timeline lanes (stroke, vibration, suction), the heatmap from `mapper.h`, chapters and bookmarks from `marks.h`.
3. Video first in the UI: `script_finder.h` with folders and variants, and the no-script card.
4. Styled controls (no default Qt Quick Controls look), `.samn` open, soft seek, recent files.
5. Core cleanups from `MODULES.md`: names, Unicode case folding, one Python module.

## Waiting on the Owner

- The four open questions in `DECISIONS.md` § Resolutions: Suite-only features; SamNPlayer 2 as the one free app plus repo visibility; AI drafts of the curve; the role of C#.
- Golden clips for the generator (P3). They stay out of the repo.
- The v0.7.0 tag in the old repo.
