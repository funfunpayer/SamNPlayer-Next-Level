# Resuming development

This guide covers recovery after losing the development environment —
something that already happened once, including loss of the entire working
directory and Go installation. It is separate from `HANDOFF.md`: that file
explains *what* the project is and why it is designed this way; this one
explains *how to resume work*.

The source version is recorded in `VERSION` and `update.BaseVersion`.
GitHub releases show the latest published version. Open work belongs only
in [docs/NEXT.md](docs/NEXT.md).

## Restoring the environment

Clone the source from [GitHub](https://github.com/funfunpayer/SamNPlayer).
Check out the corresponding tag when an exact released version is needed.
A ZIP is a supplementary backup; GitHub provides traceable history.
Before continuing, inspect `git status`, the branch, recent commits, and
`docs/NEXT.md`. Executable binaries are available in GitHub releases.

Prerequisites:

- Go as specified in `go.mod` (currently at least 1.25.0; automatic toolchain
  downloads may select a newer version required by dependencies).
- Node.js 22.12 or later with npm for the Vite frontend.
- Python with the packages in `generator/requirements.txt`; CI uses Python 3.12.
- Windows: WebView2 for the GUI. Ubuntu 24.04: `libgtk-3-dev` and
  `libwebkit2gtk-4.1-dev`; use `-tags webkit2_41` for Wails builds.
- OpenCV C++ development files (`libopencv-dev` + `pkg-config` on Ubuntu) for
  `generator/trackcv`, a cgo package - without them, `go build`/`go vet`/
  `go test` fail on that package with "opencv2/opencv.hpp: No such file".

From the repository, install the Wails CLI matching the module version
(PowerShell):

```powershell
$wailsVersion = go list -m -f '{{.Version}}' github.com/wailsapp/wails/v2
go install "github.com/wailsapp/wails/v2/cmd/wails@$wailsVersion"
python -m pip install -r generator/requirements.txt
Push-Location cmd/gui-wails/frontend
npm install
npm run build
Pop-Location
go vet ./...
go test ./...
```

The `wails` executable is installed in the `bin` directory under
`go env GOPATH`; add it to PATH. A fresh clone needs `frontend/dist` built
before Go checks, because `go:embed` requires that directory.

## Full test run

The three groups deliberately have separate commands because their
runtimes differ (the following commands use Bash):

```bash
# Go: seconds
go vet ./... && go test ./...

# Python: video tests take 1–3 minutes each
(cd generator && for t in *_test.py; do echo "== $t"; python3 "$t" || exit 1; done)

# Frontend: requires headless Chromium
for t in cmd/gui-wails/frontend/test/*_test.py; do python3 "$t" || exit 1; done
```

Frontend tests generate mocks automatically from
`frontend/wailsjs/go/main/App.js`. If a new Go method is absent because
`wails build` has not been run, tests fail with “does not provide an export
named …”. This means bindings need regeneration, rather than indicating
a defect in the test itself.

---

## Building and delivering

```bash
cd cmd/gui-wails && wails build -platform windows/amd64 -trimpath
cd ../..
cp cmd/gui-wails/build/bin/SamNPlayer.exe dist/SamNPlayer-gui-windows-amd64.exe
CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -trimpath \
  -o dist/SamNPlayer-cli-windows-amd64.exe ./cmd/cli
```

`-trimpath` is required: without it, the build machine's path, including the
username, is embedded in the binary.

Create a handoff archive:

```bash
zip -qr SamNPlayer.zip SamNPlayer \
  -x "*/node_modules/*" "*/build/bin/*" "*/__pycache__/*"
```

---

## Practices that have worked

Each point below comes from a real failure, not a stylistic preference.

**Measure instead of assuming.** Support every quality or speed claim with
evidence. Plausible explanations have proved wrong: camera compensation
was not broken, the test material was; smoothing did not destroy amplitude;
divergence made the estimate worse rather than better.

**Verify tests with a negative check.** After a regression test passes,
temporarily revert the implementation change and confirm that it fails.
Twice this prevented shipping a feature that did nothing; one insertion
had even landed in the test file instead of the pipeline.

**Document negative results.** `HANDOFF.md` contains a “Tested and rejected”
section with measurements. Without it, the same dead ends are revisited.

**Test material can be the source of an error.** Noise backgrounds provide
no trackable features for camera compensation. Moving objects must use
world coordinates so they follow camera pans. Synthetic sine videos can
also misrepresent rhythm assessment, because real material changes speed
continually.

**Keep two useful reference figures in mind:** the calibration set contains
five valid and four unusable videos. After assessment changes, the five
must still pass and the four must fail. Cache reuse was measured as 36×
faster; bypassing it makes repeated measurements unnecessarily slow.

---

## Continuing work

[docs/NEXT.md](docs/NEXT.md) contains the verified baseline, priorities,
and acceptance criteria. Existing hardware and quality limitations are
recorded in `HANDOFF.md`. Do not maintain another progress list here.
