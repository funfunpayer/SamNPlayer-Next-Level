# Video tools (ffmpeg)

SamNPlayer still needs **ffmpeg** for gray-frame decode (Windows
`simpletrack`), audio-tempo check, frame previews, and “Make playable”.
We do **not** reimplement a full H.264/HEVC stack in Go — that would be
huge and worse than shipping a known-good binary.

## How it is found (no system install required)

`videox` resolves `ffmpeg` / `ffprobe` in this order:

1. `SAMNPLAYER_FFMPEG` / `SAMNPLAYER_FFPROBE` env overrides  
2. Next to the SamNPlayer binary (portable release)  
3. `./tools/` or `./ffmpeg/` beside the binary  
4. `%Config%/SamNPlayer/tools` (user tools dir)  
5. System `PATH`

## Easy install paths

| Path | What to do |
|------|------------|
| **Portable release** (preferred) | Download `SamNPlayer-portable-*-amd64.(zip\|tar.gz)` — GUI + ffmpeg + ffprobe in one folder |
| **Settings → Install video tools** | Opt-in download of a static build into the user tools dir (no surprise network at startup) |
| System package | `apt install ffmpeg` / chocolatey / etc. — still works |

Single-file GUI assets remain for **in-app update** (replaces only the
exe; keep portable ffmpeg beside it, or use Install video tools once).

## What we build ourselves instead

Tracking signal path, Quality Doctor, audio-tempo FFT, ROI verify, and
`.samn` stay pure Go. ffmpeg is only the codec I/O helper.
