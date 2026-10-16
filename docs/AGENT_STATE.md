# Agent state

**Read this first** (new session, new agent, after a context loss), then [`MODULES.md`](MODULES.md), [`PLAN.md`](PLAN.md) and [`DECISIONS.md`](DECISIONS.md). Update this file in **every PR**, finished or not. **Push often:** the repo is always the current state.

**Last update:** 16 Oct 2026, Claude, branch `claude/samn2-p1-ui`.

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

- **P1 Player UI** (`claude/samn2-p1-ui`, Owner: an exact functional copy of 0.7, better where we can):
  - `PlayerController` on the core: video first with script folders and variants, `.samn` preferred, lanes, heatmap, chapters and bookmarks, offset per script, A/B loop, zoom window (wheel and buttons, 0.7 steps).
  - Curves as 0.7 draws them (`curve_geometry.h`): Catmull-Rom line, glow, fill, dots, live dot, tempo wave when dense. Vibration and suction lanes ("device preview" when the script has no such channel). Audio Level lane decoded by Qt (no ffmpeg).
  - Styled controls, the no-script card, error bar. A broken QML file now fails the smoke test (exit 1).
  - Tests: 60+ core cases, 13 app Qt tests (controller, zoom, decoder with a generated WAV, curve modes and layer cache).

## Next (P1, in order)

1. `docs/PARITY.md`: the full 0.7 inventory (every function, file:line), the checklist for "exact copy". A first attempt stopped at a usage limit; redo it.
2. Library panel (left) and Inspector (right), as in 0.7.
3. Heatmap range marker (select a range → zoom selection, chapter add), BPM grid, O-marker bands, feel segment bands, speed highlights, scene strip.
4. Audio Analysis card and audio segments, auto sync (audio), playback speed, `.samn` export, recent files, soft seek.
5. Core cleanups from `MODULES.md`: names, Unicode case folding, one Python module.

Owner reminders (16 Oct): the Generator has the Editor built in; detection should become better than 0.7 (C++ libraries); curves stay real curves with the audio curve; everything planned in the old roadmap comes along.

## Waiting on the Owner

- The four open questions in `DECISIONS.md` § Resolutions: Suite-only features; SamNPlayer 2 as the one free app plus repo visibility; AI drafts of the curve; the role of C#.
- Golden clips for the generator (P3). They stay out of the repo.
- The v0.7.0 tag in the old repo.
