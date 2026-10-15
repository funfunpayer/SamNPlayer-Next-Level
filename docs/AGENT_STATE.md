# Agent state

**Read this first** (new session, new agent, after a context loss), then
[`MODULES.md`](MODULES.md). Update it in **every PR**, finished or not.
**Push often:** the repo is always the current state.

**Last update:** 15 Oct 2026, Claude, branch `claude/samn2-p1`.

## Rules

- **Owner:** talk in German; the code, the interface and the docs are in
  English.
- **Branches:**
  - Claude works on `claude/*`, Grok on `grok/*`. Never push to another
    agent's branch.
  - Nothing reaches `main` without a PR.
  - Never force-push.
  - Merge only on green CI.
- **Before every push:** run `git diff --stat` and check it. Never write a
  file from a shell substitution that may be empty, and never regenerate a
  workflow from scratch. On 15 Oct, files in the old repo ended up holding
  the literal text `$(cat /tmp/...)`, and one ended with
  "[truncated ...]".
- **Never:**
  - adult media in the repo or in screenshots; screenshots use test
    patterns only;
  - model names in commits, PRs or files;
  - copied GPL, BSL or EULA code;
  - placeholders that pretend to work;
  - tests that "SKIP" with exit 0 in CI (set `SAMN_NATIVE_REQUIRED=1`).
- **Releases:** the Owner pushes tags.

## Done

- **P0 foundation** (`claude/samn2-p0`):
  - `native/` moved over from the old repo with its parity tests;
  - the Qt window with Play / Edit / Generate;
  - Play opens a funscript and draws its curve;
  - CI for Linux and Windows;
  - a module map (`MODULES.md`).
- **P1 start:** `samn/library/script_finder.py`, the script search with
  folders, tolerant names and variants. All its Go tests are ported, plus
  tests for the depth and file limits.

- **P1 Player, part 1** (`claude/samn2-p1`):
  - the Play page: a video or a script opens the other;
  - QtMultimedia video, curve with a playhead, heatmap;
  - offset saved per script, loop, shortcuts (Space, ←/→, Shift for 1 s,
    +/−);
  - `samn.formats.mapper`, ported 1:1; a sample of 11 frames matches Go
    exactly;
  - `samn.settings`.
- **Test gotcha:** pytest-qt's Qt log capture crashes when Qt's FFmpeg
  thread logs, so `--no-qt-log` is set in `pyproject.toml`.

- **P1 Player, part 2:** script variants, the "no script" card (Choose /
  Add folder; Create stays disabled until P3), drag and drop, chapters and
  bookmarks on the curve with a chapter jump, the dark theme,
  `formats.marks`, and `formats.samn_file` (byte-identical with Go).

## Next

1. **P1 rest** (Claude): open `.samn` in the Play page, soft seek, recent
   files; then the cleanup in `samn_file.py` (see `MODULES.md`).
2. **C++ ports** (Grok, `grok/*`): tracking and the camera estimate from
   `trackcv/cv.cpp`, for P3.

## Waiting on the Owner

- The role of **C#** (`docs/PLAN.md` § Languages).
- **Goal (Owner):** Go is gone at the end; optimise wherever possible.
- Golden clips for the generator measurement. They stay out of the repo.
- The v0.7.0 tag in the old repo.
