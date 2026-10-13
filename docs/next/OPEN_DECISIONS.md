# Owner decisions

## Answered (12 Oct 2026)

| # | Question | Answer |
|---|---|---|
| — | Paid product? | **No.** One program, free, financed by donations. D1–D4 (price, supporter threshold, pass scope, payment provider) and the licence pass (P7) are dropped |
| D5 | Name | "SamNPlayer Next" = the next generation of SamNPlayer, not a separate product |
| D7 | Cut AI / batch from a "Free" edition later? | Dropped — there is only one program; new features ship to everyone |
| D8 | Import timing | Imported now (Free release no longer a precondition) |
| D9 | Repo created with a README | Resolved in the import merge |
| D11 | Repo visibility | Stays public for now |

## Open

| # | Question | Context | Recommendation |
|---|---|---|---|
| D6 | **Suite:** keep AI training / golden benchmark internal, or ship them to everyone? | Owner 11 Oct: training belongs to the internal system. With no paid version there is nothing to protect commercially | Keep internal until the training UI is user-ready; then decide per feature |
| D10 | **DEV licence key in history** | `license/testdata/issuer.ed25519` is in the history (public anyway — the old repo is public). Without a licence system it protects nothing | No action. Never reuse it if keys ever return |
| D12 | **Old repo `funfunpayer/SamNPlayer`** | Owner 12 Oct: decide later | Archive after the bridge release (N1) so installed apps find updates |
| D13 | **Release source** | Installed apps check `funfunpayer/SamNPlayer` releases; the download page is `SamNPlayer-site` | Publish releases on `SamNPlayer-site`; one bridge release in the old repo ([N1](specs/N1_RELEASES.md)) |
| D14 | **Suite files in public releases** | `release.yml` attaches Suite files; both repos are public | Never publish Suite files; CI only compiles Suite |
