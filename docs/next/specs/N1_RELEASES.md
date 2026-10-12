# N1 — Releases and updater from this repo

**Status:** spec, not built · gate N1 · 12 Oct 2026
Replaces the earlier "`next` edition" draft: there is no paid Next edition
(Owner, 12 Oct — one program, free, donations). Editions stay `free` (default)
and `suite` (internal), exactly as inherited.

## Inherited state (SamNPlayer `5d2f625`)

| Piece | Today |
|---|---|
| Editions | `edition` package, `-tags suite`; Suite-only bindings return `errSuiteOnly` (`requireSuite()`, 9 files in `cmd/gui-wails`) |
| Updater | `update/update.go`: `RepoOwner/RepoName` = `funfunpayer/SamNPlayer`; asset kind `gui` / `suite-gui`, exact name match |
| Releases | `release.yml` (disabled here): on `v*` tag builds Free **and Suite** for Linux + Windows and attaches **both** to the release |
| Public copy | `scripts/publish-public-release.sh` copies only Free files to `SamNPlayer-site` |
| Latest release | `v0.5.45` (2 Oct) on `funfunpayer/SamNPlayer`; no Suite files published yet |

## Problems to solve

1. **Suite would go public.** Both repos are public. `release.yml` attaches
   Suite files to the release, so the first release after 0.6.0 would publish
   the internal Suite build to everyone.
2. **Installed apps look in the old repo.** Every installed SamNPlayer checks
   `funfunpayer/SamNPlayer` releases. If releases move here (or the old repo is
   archived), those installs never see an update unless one bridge release
   tells them where to look.
3. **Actions artifacts on a public repo** can be downloaded by any logged-in
   GitHub user, so "upload Suite as an artifact" is not private either.

## Design

### Release source: `SamNPlayer-site` (recommended, D13)

The public download page already lives in `funfunpayer/SamNPlayer-site` and
does not move when code repos change. Make it the one release source:

- `update.RepoOwner/RepoName` → `funfunpayer/SamNPlayer-site`.
- `release.yml` (re-enabled here): on `v*` tag, build Free, create the release
  on `SamNPlayer-site` with Free files + `checksums.txt` (needs a token with
  release rights on the site repo as an Actions secret, set by the Owner).
- Release notes and changelog link stay as today.

### Suite stays internal

- CI builds and tests Suite (`-tags suite`) to keep it compiling — **no upload**.
- The Owner builds Suite locally when needed (`docs/EDITIONS.md` commands), or
  D6 decides Suite features simply become part of the free program.

### Bridge for installed apps

One last release on `funfunpayer/SamNPlayer` whose binaries already contain the
new `RepoOwner/RepoName`. Installs update once from the old place and look at
`SamNPlayer-site` from then on. Only after that release may the old repo be
archived (D12).

## Tests

- `update` test: release lookup uses the configured site repo; asset match
  unchanged (`gui` never picks `suite-gui`).
- Workflow lint (`actionlint`) on the re-enabled `release.yml`.
- A dry-run job (`workflow_dispatch`, no tag) that builds and lists the files
  it would publish, and fails if any name contains `suite`.
