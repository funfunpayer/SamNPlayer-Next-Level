# N1 — The `next` edition: build tag, updater, releases

**Status:** spec, not built · gate N1 · 12 Oct 2026
Based on Free at `52d0529` (edition rename #513).

## What Free has (inherited by the import)

| Piece | Free today |
|---|---|
| `edition` package | `Suite()` compile-time constant; `suite_on.go` (`//go:build suite`) / `suite_off.go`; `Name()` = `"suite"` / `"free"` |
| GUI gate | `requireSuite()` → `errSuiteOnly` in 9 files under `cmd/gui-wails` (39 `requireSuite`/`edition.Suite()` matches); bindings exist in both builds so the JS API is identical |
| Frontend | `html[data-edition]` from `IsSuiteBuild()`; `data-suite` controls hidden by CSS in Free |
| Updater | `update.RepoOwner/RepoName` = `funfunpayer/SamNPlayer`; `guiAssetKind()` = `gui` / `suite-gui`; exact asset-name match so Free never updates into Suite |
| Releases | `release.yml` builds Free + Suite for Linux and Windows; `scripts/publish-public-release.sh` copies only Free files to `SamNPlayer-site` |

## Design

### One edition axis, three values

Replace the boolean with a name; keep `Suite()` working so inherited code
compiles unchanged.

```go
// edition_free.go   //go:build !suite && !next
const name = "free"
// edition_suite.go  //go:build suite && !next
const name = "suite"
// edition_next.go   //go:build next
const name = "next"

func Name() string { return name }
func Suite() bool  { return name == "suite" || name == "next" } // Next carries all Suite features
func Next() bool   { return name == "next" }
```

- `-tags next` alone builds Next. `next` wins if both tags are set.
- Next-only features get `requireNext()` → `errNextOnly`, same pattern as
  `requireSuite()`. Later (N7) `requireNext()` also checks the pass (P7).
- Frontend: `data-edition="next"`, controls marked `data-next`.

### Updater

| | Free | Suite | Next |
|---|---|---|---|
| Asset kind | `gui` | `suite-gui` | `next-gui` |
| Asset file | `SamNPlayer-gui-…` | `SamNPlayer-suite-gui-…` | `SamNPlayer-next-gui-…` |
| Release source | Free repo (today) | Free repo (today) | **none until N7** |

`update.RepoOwner/RepoName` become per-edition values. In the Next build the
update check is **off** until a distribution channel exists (returns "no update
channel" without a network call — the same pattern Free uses for the
placeholder owner).

### Releases while this repo is public (Owner, 12 Oct: stays public for now)

Anything attached to a release **or uploaded as an Actions artifact** on a
public repo can be downloaded by anyone with a GitHub account. Therefore, until
the Owner decides the channel (D11):

- CI builds Next for Linux and Windows **to prove it compiles and tests pass,
  without uploading binaries**.
- No `release.yml` in Next yet. No tags.
- `publish-public-release.sh` is not copied or is guarded to refuse in Next.

### Free stays untouched

- No change to the Free asset names, so a Free install never sees Next.
- The import keeps Free's `release.yml`; in Next it is disabled in the same PR
  (renamed to `release.yml.disabled` or `on: workflow_dispatch` only) so a tag
  in this repo cannot publish Free or Suite binaries from here.

## Tests

- `go test ./edition/` with no tag, `-tags suite`, `-tags next`,
  `-tags "suite next"`: `Name()`, `Suite()`, `Next()` as in the table.
- `guiAssetKind()` per edition; Next never matches `gui` or `suite-gui`.
- Update check in Next: no network call (inject a failing HTTP client).
- Every Suite-only binding works in Next; every Next-only binding returns
  `errNextOnly` in Free and Suite.
- CI matrix: Free, Suite, Next builds all green before N1 is passed.
