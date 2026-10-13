# Owner: public downloads + private source

1. `funfunpayer/SamNPlayer` stays **private** (source + CI).
2. **This** repo (`SamNPlayer-site`) stays **public** (landing + binaries).
3. After each private app release, publish the same binaries here
   (from the private checkout, as owner):

```bash
./scripts/publish-public-release.sh v0.5.11
```

4. Screenshots live under `docs/media/` and `media/` (relative paths only —
   never `raw.githubusercontent.com/.../SamNPlayer/...` once the app is private).

5. Enable **GitHub Pages** on this repo (`main` / root) so `index.html` is the
   product landing. Set the repo homepage URL to the Pages site.

Full write-up: private app `docs/CLOSED_SOURCE.md`.
