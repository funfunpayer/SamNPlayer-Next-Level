# Content sources — imported scripts & future platforms

**Status:** local import works now; **catalog/partner APIs are not built**
(and not required for Everyday Generate). See `docs/ROADMAP.md` (community
platform rejected for now) and `docs/PLATFORMS.md` (OS matrix).

---

## What works today

| Path | How |
|------|-----|
| Open / drop `.funscript` or `.samn` | Play tab |
| Multi-axis editor | Curve = General / Vibration / Suction; Drive = Recipe or Axes |
| **Optimize for Neo 2** | One click: fill gaps → Contact on → bake vibe+suction → save `.samn` |
| Bake axes / Save `.samn` / Export `.funscript` | Manual controls next to Optimize |
| Script Doctor | Diagnose gaps/speed (does not rewrite) |

Imported community scripts (other tools, future platform downloads) become
Neo-2-ready via **Optimize for Neo 2**, then you edit axes in Play.

---

## Sam Neo 2 vs “others”

| Target | Path |
|--------|------|
| **Sam Neo 2** | `.samn` with baked vibration + suction (+ Contact recipe) |
| **Community players / Handy / etc.** | Export `.funscript` (general stroke only) — no Neo axes embedded |
| Other devices later | Same stroke export; device-specific remap is a separate product lane |

---

## Future: platforms that offer video + Funscript

When we attach catalogs (download video + companion script):

1. Fetch into a local cache folder (same as drop/open today)
2. Open in Play → **Optimize for Neo 2** (or auto-offer after import)
3. Never invent stroke from the platform page — only polish / bake locally

Until then: users bring files themselves. No eroscripts/IVDB client in-tree.

---

## Ballast policy

Everyday Generate stays tip → CSRT → Contact. Research backends (Flow,
grid_lk), AI second opinion, Flow downscale stay **CLI / removed from GUI**.
Power-user scene memory is collapsed under Generate.
