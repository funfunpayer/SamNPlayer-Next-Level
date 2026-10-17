# Contributing to SamNPlayer 2

SamNPlayer 2 is the C++ / Qt successor of SamNPlayer 0.7. The goal is a
product to be proud of: every function of 0.7 and everything that was
planned, built cleanly, fast and fun to use. These rules carry over the
old repo's `CONTRIBUTING.md` and add the Owner's rules for SamNPlayer 2.

## Which document says what

| Document | What it is |
|---|---|
| `docs/PLAN.md` | Phases P0–P5 and the layers. |
| `docs/PARITY.md` | The checklist of every 0.7 function (and work after 0.7). Nothing may be forgotten. |
| `docs/DECISIONS.md` | Every decision with its reason (D-xxx). Never restart a decision from zero. |
| `docs/ROADMAP.md` | What was planned beyond 0.7; it comes along too. |
| `docs/MODULES.md` | Where each piece lives. |
| `docs/MANIFEST.md` | The Owner's master document: goals, priorities, licence, how we work. Read it first. |
| `docs/AGENT_STATE.md` | Current state and the next steps. |
| `docs/P3_GENERATOR.md` | The Generator (which is the Editor, D-137). |
| `docs/LESSONS.md` | What 0.7 learned, per area, with a rule each; tested-and-rejected ideas; gaps. |
| `docs/TEST_PARITY.md` | Every 0.7 test and where it went (ported, todo, n/a). |
| `docs/VERSIONS_AND_PLANS.md` | Every 0.x version, what was removed on purpose and why, what is still missing. |

When sources disagree, the code and reproducible tests win. The 0.7 code
(`funfunpayer/SamNPlayer`, Go + Wails + JS) is the reference for behaviour.

## Rules

1. **Parity first, then better.** Port the 0.7 behaviour, prove it, then
   improve it behind a switch when an improvement is measured (D-135).
   Tick a `PARITY.md` item only with a test or a screenshot check, and say
   where it lives (`→ SamN2: ...`).
2. **Prove it against 0.7.** Ports of formats and algorithms are compared
   with a real Go run of the 0.7 code: bytes for files, pixels for images,
   numbers to the last digit that matters. Say how in the PR.
3. **A behaviour change needs a test that fails without it.** Write the
   test, see it pass, revert the change, see it fail, restore. (In 0.7 a
   feature twice "worked" but did nothing.)
4. **Measurements, not impressions.** Claims about quality or speed carry
   numbers. Record rejected ideas and negative results too, so nobody
   repeats them.
5. **No empty shells, no placeholders, nothing unused.** A mode that is not
   built says so plainly (D-080); no buttons that do nothing, no dead code,
   no functionality without a caller.
6. **Quality and stability first, then performance.** Keep the GUI thread
   free (devices run on their worker thread), cache what is drawn often
   (the curve layer), avoid work per frame that can be done once. Do not
   rewrite for fashion; only for a measured win.
7. **Clean code.** C++17 core without Qt (`core/`), Qt only in `app/`.
   Read the surrounding code and match it. No new warnings. Errors are
   reported to the user with a clear message, never swallowed.
8. **Every platform.** CI builds and tests Linux and Windows; a test must
   pass on both. Qt Quick Controls that are customized import
   `QtQuick.Controls.Basic` (the native Windows style ignores our look).
9. **Look.** The Flow look of 0.7/0.8 (silk, glow, round Play) is the
   baseline; the Owner's visual targets decide. Check every visible change
   with a screenshot (`SAMN_SCREENSHOT=<png>`), nothing cut off or
   overlapping. Test content is synthetic only; no adult media in the repo
   or in screenshots.
10. **Devices.** Mock-only tests are marked as such; real hardware is
    named as untested until someone tests it. Emergency stop, Stop and
    device caps work everywhere and take precedence (D-037).
11. **Language.** Code, UI and docs in English; talk to the Owner in German.
    No model names in code, docs or commits.
12. **Generator = Editor.** One mode: the Generator creates and edits
    (D-137). Editor work goes into the Generator page.
13. **Read the lessons first.** Before you build or change an area, read
    its section in `docs/LESSONS.md` and the "Tested and rejected" table.
    What 0.7 removed on purpose stays out; bringing it back needs a
    measurement against 0.7 and a decision (see `VERSIONS_AND_PLANS.md`).
14. **Old tests come with the code.** When a module is ported, its 0.7
    tests are ported in the same PR, and its rows in `docs/TEST_PARITY.md`
    are updated. A port is not done while a row says `todo-now`.

## Licence of code (D-141, `LICENSING.md`)

15. **Everything is GPL-3.0-or-later.** Core and detection engine (`engine/`, a built-in plugin) alike.
16. **Copied code keeps its origin.** Code taken from GPL-3.0(-or-later), GPL-2.0-or-later, LGPL or AGPL-3.0 projects may go in, with its copyright and licence header, and a line in `THIRD_PARTY.md`. GPL-2.0-only, BSL, "source available" and proprietary code never enter.
17. **Write small things ourselves** rather than adding a dependency (manifest §4); every new dependency is named in `THIRD_PARTY.md` with its licence.

## Workflow

1. Take the next item from `docs/AGENT_STATE.md` / `PARITY.md`; one
   manageable piece per PR.
2. Branch from current `main` (`claude/...` for Claude; other agents keep
   their own prefix). Never push to another agent's branch, never force-push
   a shared branch.
3. Build and run all tests locally (`cmake --build build -j8`, `ctest`),
   check the screenshot, re-read the whole diff before committing.
4. Open a PR: problem, result, checks performed, what is still open.
5. Merge (squash) only when CI is green on Linux and Windows and the PR is
   mergeable. Never merge on red.
6. After merging: update `PARITY.md`, `AGENT_STATE.md` and, for a choice,
   `DECISIONS.md`. Delete a branch only when its changes are integrated.
7. Before a release tag: bug hunt on the tip, all tests green, the Owner
   tags.

## Work by agents

Delegated work gets a goal, the context, a bounded scope and an output
format. Every result is checked (build, tests, diff, screenshot) before it
is committed; results that do not hold up are redone, not merged.
