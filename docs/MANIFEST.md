# Manifest: SamNPlayer Next

The Owner's master document (17 Oct 2026), ordered. It is **alive**: the Owner extends it, and every agent reads it first. The original wording is in [`docs/sources/owner/MANIFEST_2026-10-17.de.md`](sources/owner/MANIFEST_2026-10-17.de.md); where this ordered version and the original seem to differ, the original wins and the difference is raised with the Owner.

## 1. Lineage
- SamNPlayer (0.x) is the predecessor and is not developed further.
- Everything it learned and found good, including the unreleased 0.8, is the basis of SamNPlayer Next (`PARITY.md`, `LESSONS.md`, `VERSIONS_AND_PLANS.md`).

## 2. Product
- **The Generator is part of the Editor**, and the Editor is a **full video editor** for creating single-axis and multi-axis funscripts (D-137).
- Output: the **standard funscript** format and the extended **.samn** format.
- It is built for people who **make their own scripts**.
- **Own engine** for motion detection from video and audio (D-140).
- **Devices are a key advantage:** drive the axes right (Sam Neo 2, Svakom and the other toys named in the docs); vibration and suction follow the motion, as SamNPlayer already did. Each device gets the mapping that fits it best, and existing scripts are adapted to it. Every function comes along.
- **Donation pages** are part of the program.

## 3. Priorities
1. **Script quality** is the highest good.
2. Then **stability**.
3. Then **performance**.

## 4. Engineering
- Each part is written in the language that gives the best experience, quality and performance for that task; languages may be combined (D-102).
- Clean, good code; no unnecessary lines; **as few dependencies as possible**. What can be written with little code is written ourselves rather than imported.
- **One core** (Player, settings, plugin system); everything else is a module around it.

## 5. Licence (decided 17 Oct: D-141)
- Goal (manifest): fully open-source capable, without showing every secret and experience we collected.
- First choice C (open core, closed engine as a separate program) was replaced the same day: the Owner wants detection as a plugin fully inside the app, with no disadvantage, "otherwise rather everything open". A closed plugin inside a GPL program is not allowed, so **everything is open under GPL-3.0-or-later**, the engine included. GPL code (for example OpenFunscripter) may be used anywhere.
- What stays private is data and notes, not code: media, golden clips, private benchmark recordings. See [`LICENSING.md`](../LICENSING.md).

## 6. Working together
- **Ask the Owner on decisions**, and look at each from at least three directions. Use existing knowledge.
- **Order:** not too much at once, no scattering. Contradicting statements are discussed and put in order.
- Several people and AI agents work on the project. Work carefully, use as few tokens as possible and as many as needed ("as much as necessary, as little as possible") – for the product too.
- **Rolling prompt:** the state is always written down so that another agent can take over if one drops out (`AGENT_STATE.md`, `CONTRIBUTING.md`, this file).
- **Bug hunting and security:** look for bugs and gaps on purpose.
