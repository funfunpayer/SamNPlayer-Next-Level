# Licensing (D-141)

SamNPlayer Next has an **open core** and a **closed detection engine**.

| Part | Where | Licence |
|---|---|---|
| Open core: Player, Editor, Training, Scene Map, settings, plugin system, file formats, devices, frame reading and generic vision building blocks, tests, CI | `app/`, `core/`, `vision/`, `tests/`, `ci/`, `.github/`, build files | **GPL-3.0-or-later** ([`LICENSE`](LICENSE)) |
| Closed engine: the generator's detection logic, tuned parameters, models, benchmark data and the experience behind them | `engine/` (from P3.1 on) and the private engine repository | proprietary, all rights reserved |
| Third-party code inside the core | as listed in [`THIRD_PARTY.md`](THIRD_PARTY.md) | its own licence, kept with the code |

## Rules

1. **The engine never links into the core.** It runs as its own program and talks to the app over the plugin process contract (`docs/P3_GENERATOR.md`, "Own engine"). This keeps the GPL core and the closed engine separate works.
2. **GPL code only in the core.** Code copied or adapted from GPL-3.0(-or-later), GPL-2.0-or-later, LGPL or AGPL-3.0 projects (for example OpenFunscripter) may go into the core. It keeps its copyright and licence header, and `THIRD_PARTY.md` names the source and version. GPL-2.0-only, BSL, "source available" and proprietary code never enter.
3. **The engine contains only our own code** and permissively licensed parts (MIT, BSD, Apache-2.0, LGPL as a shared library). No GPL code in `engine/`.
4. **Copyright of the core** stays with funfunpayer and the contributors; contributions to the core are accepted under GPL-3.0-or-later.
5. **Golden clips and private media** never enter any repository (D-073), whatever the licence.

## Before the core is made public (Owner's step)

- Move `engine/`, the engine's benchmark data and the experience documents that describe the engine's tuning (`docs/LESSONS.md` §4 and §11 detail, `docs/benchmarks/`) to the private engine repository; leave summaries in the core.
- Check the history: earlier commits contain the old proprietary notice and may contain engine experience; publish from a fresh history or a filtered one.
- Check `THIRD_PARTY.md` is complete and every copied file carries its header.
