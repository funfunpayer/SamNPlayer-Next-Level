# Wiedergabe & Codecs

Stand: September 2026 · SamNPlayer **0.5.5+**

## Wie Video abgespielt wird

1. Funscript laden → optional Video mit gleichem Basisnamen daneben
2. Go startet einen lokalen HTTP-Server (`127.0.0.1`) und liefert die Datei
   mit Range-Requests aus (`VideoFileURL`)
3. Das eingebettete **WebView** (`<video>`) dekodiert — **nicht** ffmpeg
4. Gerät folgt der Videoposition über `ReportVideoPosition` → `player.Sync`

ffmpeg wird für **Probe** und optional **H.264-Proxy** genutzt, nicht als
Player-Engine.

## Was typischerweise geht / nicht geht

| Meist OK | Oft problematisch |
|----------|-------------------|
| H.264 in `.mp4` / `.m4v` | MPEG-4 Part 2 / Xvid |
| VP8/VP9 in `.webm` | HEVC/H.265 je nach WebView |
| oft H.264 in `.mov` | rohes `.mkv` / `.avi` trotz H.264 |

## Abspielbar machen

Wenn Probe warnt oder `<video>` einen Fehler wirft:

1. Hinweisbanner im Player
2. Knopf **Abspielbar machen** → `EnsurePlayablePlaybackVideo`
3. ffmpeg erzeugt neben der Quelle: `.<name>.samnplayer-h264.mp4`
4. Wiedergabe schaltet auf die Kopie um

Voraussetzung: **ffmpeg** — portable release ships it next to the app,
or Settings → **Install video tools**, or PATH. See
[`docs/FFMPEG_TOOLS.md`](FFMPEG_TOOLS.md).

## API

| Go | Zweck |
|----|--------|
| `ProbePlaybackVideo` | Codec/Größe/Dauer + `likelyPlayable` |
| `EnsurePlayablePlaybackVideo` | Proxy erzeugen/reuse + Pfad wechseln |
| `videox.EnsurePlayableProxy` | Kernlogik |
| `EnsureVideoTools` | Opt-in static ffmpeg install into user tools dir |

## Qualitätshinweise

- Kein WebGL-Schärfen (gemessen, verworfen — `docs/NEXT.md`)
- Sync-Takt ~250 ms (`timeupdate`) — für Geräte ausreichend
- Playlist: Skripte, nicht Videos; bei Decode-Fehler kein Auto-Weiter
