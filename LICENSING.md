# Licensing (D-141)

SamNPlayer Next is **open source under GPL-3.0-or-later**, the detection engine included.

| Part | Where | Licence |
|---|---|---|
| Everything in this repository: Player, Editor, Training, Scene Map, settings, plugin system, file formats, devices, frame reading, the detection engine (built-in plugin), tests, CI | `app/`, `core/`, `vision/`, `engine/`, `tests/`, `ci/`, `.github/`, build files | **GPL-3.0-or-later** ([`LICENSE`](LICENSE)) |
| Third-party code inside the project | as listed in [`THIRD_PARTY.md`](THIRD_PARTY.md) | its own licence, kept with the code |
| Media, golden clips, private benchmark recordings | never in any repository | — |

## Rules

1. **The engine is a built-in plugin.** It runs inside the app through the plugin interface (`docs/P3_GENERATOR.md`, "The engine is a built-in plugin") and ships with every build; there is no separate engine program.
2. **Copied code keeps its origin.** Code copied or adapted from GPL-3.0(-or-later), GPL-2.0-or-later, LGPL, AGPL-3.0 or permissively licensed projects (for example OpenFunscripter) may go in. It keeps its copyright and licence header, and `THIRD_PARTY.md` names the source and version. GPL-2.0-only, BSL, "source available" and proprietary code never enter.
3. **Copyright** stays with funfunpayer and the contributors; contributions are accepted under GPL-3.0-or-later.
4. **Golden clips and private media** never enter any repository (D-073), whatever the licence.

## Before the repository is made public (Owner's step)

- Decide which experience notes stay private (for example raw benchmark logs that name private clips); the code is open either way.
- Check the history: earlier commits carry the old proprietary notice; publish from a fresh history or a filtered one if that matters.
- Check `THIRD_PARTY.md` is complete and every copied file carries its header.
