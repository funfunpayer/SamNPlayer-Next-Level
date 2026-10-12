# Patch-System / Patch channel (SamNPlayer)

**Status:** Design + MVP (query → ask → apply).  
**Scope:** Additive hotfix channel beside full Releases/Updates.  
**Out of scope:** Advanced Generator cleanup; Virtual Person (#264 cancelled / closed).

---

## DE — Kurzfassung

| Kanal | Tag / Form | Zweck |
|-------|------------|--------|
| **Release / Update** | `vX.Y.Z` | Normale, versionierte Auslieferung. Unverändert. |
| **Patch** | `patch-vX.Y.Z-pN` | Hotfix *für eine bestehende Basisversion*. Additiv. |

Patches **ersetzen keine Releases**. Weiterhin normal taggen, bauen, releasen. Patches sind ein zweiter, abfragbarer Kanal mit demselben UX-Muster wie Update heute: prüfen → Nutzer fragen → sicher anwenden (Download + Neustart).

---

## EN — How patches relate to releases

1. **Full release** remains the primary ship path (`VERSION` / `update.BaseVersion`, tag `vX.Y.Z`, `.github/workflows/release.yml`, GitHub `/releases/latest`).
2. **Patch** targets users already on a base (e.g. `v0.5.36`) who need a fix *before* the next full cut (`v0.5.37`).
3. Applying a patch **does not** retire the release channel. After a full update to a newer `v*`, patch offers for the old base are irrelevant.
4. **Priority in the app:** if a newer full release exists → offer **Update** only. Else if a matching unapplied patch exists → offer **Patch**. Never hide a full update behind a patch.

---

## Query API (client)

Package `update` (alongside existing `CheckLatest`):

| Symbol | Role |
|--------|------|
| `CheckLatestPatch(currentVersion)` | List GitHub releases, match tag `patch-vX.Y.Z-pN` for current base, skip already-applied IDs; return highest `N` or `(nil, nil)`. |
| `Patch` | `ID`, `TagName`, `BaseVersion`, `PatchNumber`, `HTMLURL`, `Body`, `Assets` |
| `(*Patch).AssetForThisPlatform(kind)` | Same asset naming as releases (`SamNPlayer-gui-…`). |
| `(*Patch).ChecksumFor(name)` | Reuses `checksums.txt` + SHA-256 path. |
| Applied state | `%APPDATA%/SamNPlayer/applied_patches.json` (Linux/macOS: `~/.config/SamNPlayer/…`) |

GUI bindings (`cmd/gui-wails`):

- `CheckForPatch()` → same shape as `UpdateCheckResult` (+ `kind: "patch"`, `patchId`)
- `ApplyPatch()` → download + verify + `ApplyAndRestart` (marks applied before exit)

Startup / Settings reuse the existing Update UX (banner or Settings status + Download & restart). Errors on startup stay log-only (no alert popup).

---

## Publishing a patch (ops)

1. Build GUI/CLI binaries for the **same base** (or fix-only rebuild); optional ldflags still `vX.Y.Z` — applied-patch state tracks the patch ID.
2. Create a GitHub Release with tag **`patch-vX.Y.Z-pN`** (example: `patch-v0.5.36-p1`).
3. **Mark it as prerelease** so it never becomes `/releases/latest`.
4. Attach the same asset names as a normal release + `checksums.txt`.
5. Do **not** use a `v*.*.*` tag (that would fire `release.yml` and look like a full cut).

Client also hardens `CheckLatest`: only tags matching plain `vX.Y.Z` count as full releases (defense in depth if a mistaken non-prerelease appears).

---

## Security / signing

Existing Update patterns (reused for patches):

- HTTPS + host allowlist (`github.com` / `*.githubusercontent.com`)
- Optional SHA-256 via release `checksums.txt`
- Size cap on download; mismatch aborts apply

**Not in MVP:** Ed25519 binary signing (license tokens already use Ed25519 in `license/`; updates do not yet). Future: optional signature asset next to checksums without changing UX.

---

## File layout

| Path | Role |
|------|------|
| `update/update.go` | Full release path (kept intact; minor tag filter) |
| `update/patch.go` | Query / parse / match patches |
| `update/patch_state.go` | Applied-patch persistence |
| `update/patch_test.go` | Unit tests |
| `cmd/gui-wails/app_update.go` | `CheckForPatch` / `ApplyPatch` + update priority |
| `cmd/gui-wails/frontend/src/main.js` | Startup: update first, else patch banner |
| `cmd/gui-wails/frontend/src/settings.js` | Manual check: update, then patch |
| `cmd/gui-wails/frontend/wailsjs/…` | Hand-updated bindings for new methods |

No change to `release.yml` for MVP. Optional later: `workflow_dispatch` patch workflow.

---

## UX copy (EN; DE optional later)

- Banner: `Patch {id} available (you have {version}).` — buttons **Download & restart** / **Later**
- Settings: after “No update available”, if patch found → `Patch {id} available…` + apply button
- Same silence on startup network errors as Update (log only)

---

## Tests (high level)

- Unit: tag parse, base match, skip applied, prefer higher `pN`
- Unit: `CheckLatest` ignores non-`vX.Y.Z` tags
- Existing `./update` + frontend `update_check_test.py` stay green; patch Settings path covered by a small harness extension or Go-side tests
- Full release apply path unchanged (`Download` / `ApplyAndRestart` / Settings Update button)

---

## Coordination

Sibling worker **Fix Update slash popup** may touch `main.js` / Settings update UI. This channel only *adds* patch checks after Update; rebase onto that PR if the same hunks conflict.
