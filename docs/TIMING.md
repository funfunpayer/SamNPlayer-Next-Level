# Timing of the device path

**Question (Owner, 5 Nov 2026):** is the Player as exact and as fast as the Go version (0.7)? Until now nothing of the real end-to-end timing had been measured; the logic is a port with parity tests. This file describes the path as the code has it, compares it with the Go original, and gives the first measured numbers (`tests/app/timing_bench.cpp`). Nothing was changed in the behaviour.

## 1. The path

### 1.1 Where the playhead comes from

There are two sources; the Player page uses one of them (`app/qml/PlayerPage.qml:88-117`, `page.player` is `clock` without a video, `media` with one).

- **Script without a video (`ScriptClock`).** `app/src/script_clock.cpp:8-12`: a `QTimer` with `setInterval(16)`; no `setTimerType` is called, so it is the default `Qt::CoarseTimer`. The position is not counted in ticks: every read computes `basePos + (now - baseTime) * rate` from a `QElapsedTimer` (`script_clock.cpp:23-27`, monotonic), so late ticks never add up to drift; a tick only samples it (`tick()`, `script_clock.cpp:90-103`, emits `positionChanged` when the ms value changed).
  - Qt 6.4.2 and 6.7.3 (the Windows release build, `.github/workflows/ci.yml:33-38`) both turn a `CoarseTimer` of 20 ms or less into a `PreciseTimer` (qtbase `src/corelib/kernel/qtimerinfo_unix.cpp` v6.4.2 lines 431-446; `qeventdispatcher_win.cpp` v6.4.2 lines 303-316 and 343-360, v6.7.3 lines 316-318 and 358). On Windows such a timer is a multimedia timer (`timeSetEvent`, 1 ms resolution), not the 15.6 ms `SetTimer`. So the 16 ms clock and the 20 ms ramp timer are already precise timers on both systems; the 15.6 ms granularity applies to coarse timers above 20 ms (the 1.5 s watchdog, the 2 s device tick). This is read from the Qt sources, not measured on Windows.
- **Video (`MediaPlayer` = `QMediaPlayer`).** `PlayerPage.qml:196` `onPositionChanged: page.playheadMoved(media.position)`. How often that fires is the media backend's choice: the FFmpeg backend reports every **100 ms** in Qt 6.4.2 (qtmultimedia `src/plugins/multimedia/ffmpeg/qffmpegmediaplayer.cpp` v6.4.2 lines 33-35, `PreciseTimer`) and every **50 ms** in Qt 6.7.3 (same file, v6.7.3 lines 40-42). The comment in `playback_output.h:27-28` ("about 4 to 10 times a second") describes 0.7's `timeupdate`, not this.

Both go through `playheadMoved(pos)` (`PlayerPage.qml:97-101`) to `PlayerController::setVideoPositionMs` (`app/src/player_controller.cpp:1150-1162`): one `samn::script_time(video, offset)` = `max(0, video - offset)` (`core/src/playback.cpp:18-20`) feeds the curves, the heatmap and `PlaybackOutput::update`. The offset is applied exactly once (LESSONS PS-01). The play speed reaches the clock / media player (`playbackRate: page.speed`) and `PlaybackOutput::speed` (`PlayerPage.qml:117`).

### 1.2 `PlaybackOutput` (GUI thread)

`app/src/playback_output.cpp`, constants in `playback_output.h:102-112`.

- **`update(t)`** (`:202-232`) restarts the stale watchdog, detects a jump (back > 250 ms, forward > 1.5 s: soft seek), starts the soft start after a quiet period, then sends levels, stroke and axes for script time `t`. Every value is sampled at the report; nothing is scheduled ahead.
- **Levels** (`sendLevels`, `:234-261`): vibration and suction = `PositionAt(lane, t)` through Reduce peaks, the Extended-O scale, the soft-start factor and channel level x intensity. A value is sent only when it moved by **0.005** or more from the last sent one (`:249`, `:256`).
- **Stroke** (`sendStroke`, `:273-297`): the next action after `t` (`upper_bound`), sent once, as "move there in `max(20, (at - t) / speed)` ms" (`moveMs`, `:263-266`). The move's *end* is anchored to the action's due time; its *start* is the report that first sees the previous action passed. A soft seek makes the move at least `way x 500 ms`; a speed limit makes it at least `ceil(way x 100 x 1000 / limit)` ms (`speedLimitMs`, `:268-271`) after the stroke filter (`core/src/stroke_filter.cpp:151-169`, which lowers the amplitude) has run. Extra axes the same way without the speed limit (`sendAxes`, `:299-323`).
- **Timers:** stale watchdog `kStaleMs` = 1500, single shot, coarse (`:51-56`); a pause, stop or the watchdog runs `goQuiet` (`:325-341`: Stop, optional 1.2 s home move). Ramp timer `kRampTickMs` = 20 (`:58-59`, `rampTick`/`updateRampTimer` `:499-511`) re-sends the levels while the soft start (10 steps over `softStartMs`, default 500, `core/src/output_levels.cpp:14-17, 33-36`) or an Extended-O cycle runs.

### 1.3 `DeviceController` and its worker thread

`app/src/device_controller.cpp`. Every device and transport lives on one worker `QThread`.

- `setVibration` / `setSuction` / `setPosition` / `setAxis` (`:997-1009`) only store the value in a pending batch under a mutex (`Queue*`, `:111-134`): **newest wins** per channel. The first value of a batch posts one `Flush` task (`ScheduleFlushLocked`, `:301-305`); later values ride along until the worker takes the batch (`Flush`, `:307-316`). A value for another device generation first clears the batch (`ForGenerationLocked`, `:289-299`); the worker drops a batch of an earlier generation (`ApplyLevels`, `:714-717`).
- `Post` (`:97-106`) appends a task and wakes the worker with a queued invocation; `Drain` (`:268-287`) runs the tasks one after another. **Stop and the emergency stop jump the queue** (`Post(..., true)`) and drop the pending batch first (`:1037-1046`).
- `ApplyLevels` (`:706-737`) writes the batch in a fixed order: vibration, suction, position, then the axes, **each a separate transport write**, then `CheckAlive`.
- The 2 s worker tick (`EnsureInit`, `:251-266`) runs keepalive / ping / battery as a queued task: a battery read is a GATT read on the Sam Neo 2 (cached `kBatteryCacheTtl` = 30 s, `core/include/samn/device/battery.h:16`) and a request/reply on Intiface (bounded by `kIntifaceBatteryWait` = 3 s, `intiface.h:41`); a level batch posted meanwhile waits behind it.

### 1.4 The transports

| Kind | Write | What it waits for |
|---|---|---|
| Sam Neo 2 (BLE) | `SamNeo2Device::WriteNow` (`core/src/device_samneo2.cpp:115-124`) with response (`protocol.h:43`); an unchanged packet is skipped (`:92-101`) | `BleTransport::Send` (`app/src/device_io/ble_transport.cpp:226-259`) blocks the worker until `characteristicWritten` (timeout 3 s, `ble_transport.h:32`): one GATT round trip per packet, two per tick when both channels change |
| TCode UDP | one datagram per line (`socket_line_transport.cpp:112-121`, `device_tcode.cpp:194-202`) | nothing |
| TCode TCP | `write` + `waitForBytesWritten(2000)` | the kernel accepting the bytes. `QAbstractSocket::LowDelayOption` is not set (no `TCP_NODELAY`), so Nagle may hold a second small line until the first is acknowledged |
| TCode serial | `write` + `waitForBytesWritten(1000)` (`serial_line_transport.cpp:82-92`) | the driver; 115200 8N1 moves a 12-byte line in about 1 ms |
| Intiface (WebSocket) | `sendTextMessage` + `flush` (`websocket_transport.cpp:128-135`); `IntifaceDevice::Send` does not wait for the server's `Ok` (`core/src/device_intiface.cpp:102-115`) | nothing; then the Intiface server and its own device link |

### 1.5 What each stage can add (worst case, from the code)

| Stage | Adds | Note |
|---|---|---|
| Script clock sampling | 0 to 16 ms, plus any time the GUI thread is busy | precise timer (see 1.1) |
| Video position sampling | 0 to 100 ms (Qt 6.4.2 FFmpeg), 0 to 50 ms (Qt 6.7.3), plus GUI-thread busy time | GStreamer backend not checked |
| QML hop `playheadMoved` | not measured | one JS call per report |
| `PlaybackOutput::update` | measured below | |
| Change threshold 0.005 | a change below 0.5 % of full scale is not sent | |
| Ramp timer | a soft-start / Extended-O step up to 20 ms late (or until the next report) | |
| Stroke start | late by the sampling lag; the move is shorter by the same amount (end stays on time) | |
| Speed limit (when it binds) | the end is late by up to the sampling lag | `dur >= ceil(way x 100000 / limit)` |
| GUI -> worker hand-off | mutex + queued invocation | measured below |
| A BLE write with response | one round trip per packet, serial: the second channel waits for the first | radio not measured |
| Worker 2 s tick | a level may wait behind a battery read (every 30 s on the Neo) | |
| Stale watchdog | silence 1.5 s (coarse: up to 5 % later) after the last report | intended |
| TCP without `TCP_NODELAY` | a second line within one round trip may wait for the ACK (delayed ACK: up to 40 ms on Linux, 200 ms on Windows) | not seen on loopback |

## 2. The Go original (0.7)

The Go sources are not in this repository; the 0.7 repository (`SamNPlayer`, commit `7c69073`) was read for this comparison.

- **With a video (`player.Sync`)** the device follows the video element's `timeupdate`, "about every 250 ms" (`cmd/gui-wails/frontend/src/playback.js:4138-4162`; `player/sync.go:15-20, 31-32`). `ReportVideoPosition` subtracts the offset and hands the position to a channel of 4 that drops a report when full (`cmd/gui-wails/app_playback.go:267-273, 315-332`). Each report writes the frame at that position (`frameAt`, frames on the 50 ms mapper grid, `funscript/mapper.go:159, 219-226`) synchronously on the player goroutine (`setOutput`, `sync.go:156-180`), then `driveStroke` with the same formula as SamN2: `max(20 ms, (a.At - posMs) / speed)` (`player/stroke.go:143-189`). No coalescing: a slow BLE write delays the loop.
- **Without a video (`player.Play`)** the goroutine waits with `time.After` until the absolute due time of each 50 ms frame (`realTimeFor`, `player/player.go:245-253`; loop `:303-341`): deadline scheduling on the frame grid, strokes evaluated only at frame times.

So 0.7 sampled the video at about 4 Hz and the clock-only playback at 20 Hz (aligned to the frames). SamN2 samples the video at 10 Hz (Qt 6.4) or 20 Hz (Qt 6.7) and the clock at 62.5 Hz. Both compute the stroke duration from the sampled position, so in both the stroke *end* is on time and the *start* is late by the sampling lag. Go's timing was never measured either; its Windows timer resolution was not checked here.

## 3. The measurement

`tests/app/timing_bench.cpp` (target `timing_bench`, built with the tests, **not** registered with ctest). It plays synthetic scripts through the real `ScriptClock` (or a `QMediaPlayer` on `tests/app/data/testpattern_4s.mp4`), `PlaybackOutput`, `DeviceController` with its worker, and the real TCode UDP/TCP and Intiface WebSocket transports, into local stand-ins that stamp every write with `std::chrono::steady_clock`:

- `neo`: a Sam Neo 2 stand-in transport (the stamp is taken when the worker hands the packet over); `neo-ack15` adds a 15 ms acknowledgement per write (a model of a GATT write with response, not a measured radio);
- `udp` / `tcp`: a TCode line sink on its own thread (stroke L0 and twist R0);
- `ws`: a Buttplug v3 server on its own thread (vibration and a linear axis).

Scripts: strokes about every 400 ms (10 <-> 90, twist 30 <-> 70), vibration steps about every 100 ms, suction about every 400 ms. The due times are moved by `(37 i) mod span` so they meet every phase of the 16 ms tick and the 100 ms media report (an exact 400 ms grid is a multiple of both and would always meet them at one phase). Each due time is mapped to the steady clock from the reports themselves: for the script clock (linear by construction) the report least behind its position is the anchor; for the media player the mapping follows the media clock's own reports piecewise, because that clock can stall and the picture follows it. A write is matched to its step only before the same value is due again, so a step whose value was coalesced away is counted as such and never borrows a later write.

Metrics: report interval and jitter; report -> wire (the pipeline); wire - due (levels); move start - due and move end - due (strokes); duration - formula (the sent duration against `max(20, (at - t) / speed)` and the speed-limit bound; 0 means exact); interval between sends; with an offset also "due, offset ignored" (must move by exactly the offset). Scenarios: baseline (60 s) with no load and with 3 busy threads, offset +/-200 ms and speed 0.5x / 2x (20 s each), a 150 u/s speed limit, the soft start (5 starts), and the media player (5 plays of the 4 s clip, with and without load).

Run it by hand:

```
cmake --build build --target timing_bench
build/tests/app/timing_bench                   # all scenarios, about 15 min
build/tests/app/timing_bench --seconds 10 --only udp,neo
```

## 4. Results (5 Nov 2026)

**Setup:** 4 cores (shared VM), Linux 6.18, Qt 6.4.2, `QT_QPA_PLATFORM=offscreen`, FFmpeg media backend, unoptimised build (empty `CMAKE_BUILD_TYPE`). Three repetitions, 15:50 to 16:35 UTC, nothing else compiling. Values: the median of the three repetitions' p50 / p95 / p99, and the largest max of all three. The loadavg (1 min) is the range at the start of each scenario over the three repetitions; "+load3" runs with 3 busy threads, which make up most of its loadavg. Every row with every repetition's p99 and both loadavg ends: [`benchmarks/TIMING_2026-11-05.md`](benchmarks/TIMING_2026-11-05.md).

### 4.1 Playhead reports

| Source | loadavg | interval p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| Script clock (16 ms timer), all baseline runs | 0.04-1.35 | 16.2-16.3 | 16.4 | 16.9-17.5 | 61-112 |
| Script clock, +load3 | 0.02-3.16 | 16.2 | 16.3-16.5 | 17.7-18.8 | 46-84 |
| `QMediaPlayer` (FFmpeg, Qt 6.4.2) | 0.31-0.53 | 100.3 | 100.5 | 102.4 | 103.3 |
| `QMediaPlayer`, +load3 | 0.20-0.38 | 100.3 | 100.5 | 102.9 | 200.0 |

The script clock's jitter `abs(interval - 16)` is 0.3 ms at p50 and 1.7-2.7 ms at p99. The max is a single late report per run or so (1 in about 3700). `PlaybackOutput::update` takes 28-43 us at p50 and 0.11-0.16 ms at p99 (up to 0.54 ms with load; unoptimised build), but one call in the first UDP run took 105 ms: that is the run's 105 ms report gap and its 124 ms stroke start. The cause (the thread descheduled, a page fault, a lock held by the worker) was not identified. The media clock fell more than 4 ms behind a steady clock once in the 30 plays: a stall of 102 ms under load.

### 4.2 Levels and strokes, script clock, 60 s

| Scenario | loadavg | channel | report -> wire p50 / p99 / max | wire (or move start) - due p50 / p95 / p99 / max | move end - due p50 / p99 / max |
|---|---|---|---|---|---|
| Sam Neo 2 stand-in | 1.01-1.35 | vibration | 0.28 / 1.99 / 5.0 | 8.3 / 15.5 / 16.5 / 47.8 | |
| | | suction | 0.28 / 1.57 / 3.4 | 8.5 / 15.8 / 16.6 / 30.9 | |
| Neo, 15 ms ack model | 0.35-0.49 | vibration | 0.27 / 2.17 / 5.3 | 9.4 / 16.6 / 17.4 / 20.1 | |
| | | suction | 0.27 / 16.76 / 19.8 | 10.3 / 29.9 / 32.3 / 40.0 | |
| TCode UDP | 0.13-0.30 | stroke L0 | 0.50 / 3.25 / 25.6 | 8.8 / 16.1 / 16.8 / 123.6 | 1.03 / 4.37 / 26.6 |
| TCode TCP | 0.04-0.18 | stroke L0 | 0.52 / 2.74 / 3.0 | 8.4 / 15.9 / 17.8 / 33.5 | 1.04 / 3.73 / 4.2 |
| Intiface WebSocket | 0.06-0.10 | vibration | 0.67 / 2.52 / 89.0 | 8.9 / 16.0 / 16.9 / 91.4 | |
| | | stroke | 0.67 / 3.21 / 37.1 | 9.1 / 15.9 / 16.6 / 51.0 | 1.21 / 3.72 / 38.0 |
| Neo, +load3 | 0.02-0.07 | vibration | 0.12 / 2.71 / 5.1 | 8.2 / 15.4 / 16.1 / 33.1 | |
| Neo 15 ms ack, +load3 | 1.91-2.08 | suction | 0.13 / 19.17 / 20.6 | 10.1 / 29.7 / 31.2 / 35.0 | |
| UDP, +load3 | 2.64-2.84 | stroke | 0.24 / 5.20 / 5.7 | 8.6 / 15.6 / 16.3 / 71.4 | 0.82 / 5.39 / 7.9 |
| TCP, +load3 | 2.69-3.16 | stroke | 0.27 / 3.12 / 6.1 | 8.4 / 15.7 / 16.3 / 17.4 | 0.73 / 3.44 / 6.7 |
| WebSocket, +load3 | 2.89-3.14 | vibration | 0.38 / 4.76 / 8.3 | 8.4 / 15.7 / 17.0 / 29.4 | |
| | | stroke | 0.39 / 3.37 / 5.4 | 8.8 / 16.1 / 16.5 / 21.4 | 0.93 / 4.12 / 6.4 |

The twist axis R0 behaves like L0 (same tick, the next write). Every step and every move was found on the wire: none was coalesced away, none fell between two reports. The interval between sends follows the script (vibration 95.6 ms p50, strokes 431.7 ms p50 with the spread due times).

### 4.3 Offset, play speed, speed limit, soft start, video

| Scenario | loadavg | What is checked | Result (median of 3; max) |
|---|---|---|---|
| UDP offset +200 / -200 | 2.02-3.09 | move end - due, offset ignored (should be +/-200 plus the 1 ms of 4.2) | +201.0 / -198.9 (max +202.8 / -196.4) |
| Neo offset +200 / -200 | 0.41-0.63 | level wire - due, offset ignored (should be +/-200 plus the 8 ms sampling lag) | +208.3 / -191.5 |
| UDP speed 0.5x / 2x | 0.71-1.52 | duration - formula; move end - due p99 | 0 in all 288 moves; 3.2 / 2.9 ms |
| Neo speed 0.5x / 2x | 0.15-0.32 | vibration wire - due p99 | 26.4 / 16.6 ms (per repetition at 0.5x: 63.0 / 16.6 / 26.4; one write waited 59 ms) |
| UDP speed limit 150 u/s | 0.13-0.17 | speed on the wire; duration - formula; move end - due | max 149.77 u/s (never above); 0 in all 144 moves; end late by 5.1 p50, 13.6 p99, 14.6 max |
| Soft start 500 ms (5 starts x 3) | 0.09-0.20 | first step after `play()`; step k vs (k-1) x 50 ms after step 1 | 16.6 ms (one tick); 2.6 p50, 13.0 p99, 15.7 max |
| Soft start, +load3 | 0.08-0.17 | the same | 16.5 p50, 21.7 max; 2.7 p50, 11.3 p99, 17.2 max |
| Video, `QMediaPlayer` 4 s clip x 5 | 0.31-0.53 | stroke report -> wire; move start - due; move end - due (vs the media clock) | 0.55 / 1.8 p99; **41.9 p50, 89.8 p95, 91.7 max**; 0.65 p50, 2.4 p99, 4.0 max |
| Video, +load3 | 0.20-0.38 | the same | 0.25 / 1.0 p99; 41.5 p50, 89.5 p95, 89.7 max; 0.22 p50, 1.9 p99, 3.6 max |

### 4.4 Caveats

- **No real device, no radio.** The Sam Neo 2 is a stand-in: "wire" is the moment the worker hands a packet to the transport. A real GATT write with response adds a round trip per packet (connection interval, typically 7.5 to 50 ms, unknown for this device and for Windows' stack); the 15 ms model only shows how the two channels queue behind each other. Intiface adds its server and its own device link; TCode devices add their firmware's parsing and motion planning. None of that is in these numbers.
- **Linux, not Windows.** The release runs on Windows with Qt 6.7.3. The timer conversion (see 1.1) and the 50 ms media report of Qt 6.7.3 are read from the Qt sources, not measured. Windows' scheduler, socket stack (delayed ACK up to 200 ms) and BLE stack were not measured.
- **Offscreen, no window.** The GUI thread did not render QML, curves or video frames; in the app those share the thread with the reports, so its stalls can be longer. The QML hop `playheadMoved` is not in the measurement.
- **Unoptimised build**, shared VM, loopback sockets; the sinks' own wake-up time is included in "wire" (an upper bound).
- **Resolution:** the script clock and the sent durations are whole milliseconds, so +/-1-2 ms is the resolution of "move end - due".
- **Media clock:** "due" for the video path is the media clock's own position; whether the picture on screen matches it was not measured. The clip is a 4 s test pattern.
- **Earlier runs while another build ran** (loadavg 2.5 to 12.8, same scenarios, before the last analysis fix) showed the same medians but gaps of the GUI thread up to 235 ms; they are not in the tables.

## 5. Findings

1. **The software pipeline is fast.** From a playhead report to the transport takes 0.2 to 0.7 ms at p50 and 1.6 to 5 ms at p99 on every transport, also with 3 busy threads on 4 cores (a 15 ms acknowledgement aside). Coalescing never dropped a step at these rates.
2. **The error is the sampling of the playhead, not the sending.** Levels and stroke starts are late by the time since the last report: 8 to 9 ms p50 and about 16 to 17 ms p99 with the script clock; with a video (Qt 6.4.2) **42 ms p50 and 90 ms p95**. With Qt 6.7.3 (the release) the media reports come every 50 ms, so about half of that is expected (not measured).
3. **Stroke ends are on time.** Every move's duration is computed from the sampled position, so it ends 1 ms (p50) and 4 ms (p99) after its due time, also on the video path (0.65 ms p50) and at 0.5x / 2x. The price is the start: a 400 ms stroke that starts 90 ms late is squeezed into 310 ms, 29 % faster than scripted (with 50 ms reports up to 14 %, with the script clock up to 4 %).
4. **The formulas hold exactly.** The sent duration equals `max(20, (at - t) / speed)` and the speed-limit bound in all measured moves; the speed on the wire never exceeded the limit (149.77 for 150 u/s). When the limit binds, the move cannot be squeezed, so its end is late by the start lag (5 ms p50, 14.6 ms max with the script clock; with a video up to the report interval).
5. **The offset is exact.** +/-200 ms moves the on-wire timing by +/-200 ms (+201.0 / -198.9 for strokes, the rest is the 1 ms of finding 3).
6. **Soft start:** the first step goes out with the first report (16.6 ms after `play()`); the later steps are 2.6 ms (p50) to 15.7 ms (max) behind their 50 ms grid, from the 20 ms ramp timer and the 16 ms reports.
7. **A Bluetooth write with response serialises the channels.** With the 15 ms model, suction changes at the same tick as vibration go out one acknowledgement later (report -> wire p95 15.6 ms, wire - due p99 32 ms).
8. **Load:** 3 busy threads on 4 cores changed p99 by at most a few ms. Rare single gaps are the worst case: a report interval up to 112 ms, one `update` call of 105 ms, a report -> wire of 89 ms (Intiface vibration), about once per 60 s run and not more often under load; the cause was not identified. A gap delays everything queued behind it.
9. **Qt timers:** `Qt::PreciseTimer` would change nothing for the 16 ms clock and the 20 ms ramp: Qt 6.4 and 6.7 already treat coarse timers of 20 ms or less as precise, on Windows with the multimedia timer (Qt sources, 1.1).
10. **TCP:** no Nagle stall was seen on loopback (stroke and twist lines arrived in one read). `TCP_NODELAY` is not set; a real device or Windows may behave differently.
11. **Against 0.7:** with a video, 0.7 took a position about every 250 ms (`timeupdate`), SamN2 every 100 ms (Qt 6.4) or 50 ms (Qt 6.7); without a video, 0.7 sent on its 50 ms frame grid at the frames' due times, SamN2 samples every 16 ms. In both the stroke end is on time and the start is late by the sampling lag. So SamN2 is at least as exact as 0.7 in every mode, and finer with a video; the remaining error is the sampling of the playhead, mainly on the video path.

## 6. Recommendations (ranked; 1 is implemented, see § 7)

The numbers show one real problem: the video path's stroke start (and level) lag of up to 50 ms (Qt 6.7) or 100 ms (Qt 6.4). The rest is at the resolution of the devices.

1. **Extrapolate the video playhead between media reports.** Keep the last report (position, steady time, rate) and let the 16 ms clock drive `PlaybackOutput::update` with `pos + (now - at) x rate` while the video plays; take every new report as the new anchor; never extrapolate further than one report interval plus a margin; reset on seek, pause, buffering, end and a rate change.
   - Gain: the video path behaves like the script clock: start / level lag from 0-100 ms (0-50 ms with Qt 6.7) to 0-16 ms; strokes no longer squeezed by up to 29 %.
   - Risk: a stalling media clock (one 102 ms stall measured) is overrun by up to the cap; double reports around seeks; the loop jump (`loopJumpFor`). PS-01 (the offset in one place) must stay: extrapolate the video time, then the one `script_time`.
   - Tests that protect it: `player_controller_test` `offsetSavedPerScriptAndPlayhead`, `loopOverMarkedRange`; `playback_output_test` `seekEasesIn`, `staleWatchdogStopsTheDevice`, `pauseStopsAndMovesHome`, `pauseInsideTheSoftStartStopsAtOnce`; this bench's `media` scenario (move start - due should fall to about 8 ms p50).
2. **Send the next stroke at its start time (look-ahead), not at the next report.** From the (extrapolated) playhead, arm a precise single-shot timer for the due time of action i-1 and send the move to action i then, with the duration to action i's due time.
   - Gain: stroke start lag about 1 ms on both paths (from 8 ms p50 / 16 ms p99 with the script clock); no squeezing; the speed limit then no longer pushes the end late.
   - Risk: a second timing path beside the reports (seek, pause, emergency stop and speed changes must cancel or re-arm it; the PS-04 rule "no state may turn a stop into a no-op"); soft seek and the speed limit must stay in one place.
   - Tests: `playback_output_test` `strokerGetsTheNextActionWithSafetyRange`, `seekEasesIn`, `speedLimitFilterShapesWhatIsSent`, `strokeOffSendsNothing`, the emergency-stop tests; this bench (duration - formula must stay 0).
3. **Send level changes at their due time.** The lanes are 50 ms frames (`mapper.h` `tick_ms`); a timer to the next frame boundary would put a level step about 1 ms after its due time instead of 0-16 ms. Low value: no motor or pump answers within 16 ms, and the real device response is not measured. Tests: `softStartRampsTheLevels`, `extendedORampsDownHoldsAndBack`, the change threshold (DV-01).
4. **Set `QAbstractSocket::LowDelayOption` on the TCode TCP socket.** One line; removes a possible Nagle + delayed-ACK wait (40 ms Linux, 200 ms Windows) when two lines go out back to back (stroke + axis, `DSTOP` + home). Not observed here, so only on evidence from a real TCP device or as cheap insurance. Test: `device_transports_test`.
5. **Measure before touching Bluetooth.** The ack round trip is the largest unknown; the diagnostics run already records write latency on the Owner's device. Only then decide on anything like combined or unacknowledged writes (D-049: no guessed device numbers).
6. **Not recommended:** `Qt::PreciseTimer` (no effect, finding 9) and a new sender thread with its own clock (the worker already is a dedicated thread; the hand-off costs well under 1 ms at p50 and a few ms at p99, finding 1). G2.5 latency compensation (a device-latency offset) needs the device and radio measurements first; the software part is about 1 ms.

## 7. Video playhead extrapolation (measured)

**Owner decision (5 Nov 2026, D-164):** recommendation 1 is implemented. Only the video path of the Player changed; the script clock, the Editor, Training and the Generator (its own `MediaPlayer`, no device output) are untouched.

### 7.1 What the Player does now

- **Where:** `core/include/samn/playhead.h` (`PlayheadExtrapolator`, plain C++, the rules), `app/src/media_playhead.*` (`MediaPlayhead`: the monotonic clock and the tick), `PlayerController::setMediaPlaying` / `reportMediaPosition`, and `PlayerPage.qml`, whose `MediaPlayer` now reports there. The `ScriptClock` still calls `setPlaying` / `setVideoPositionMs`, exactly as before.
- **Estimate:** last report + (now - time of that report) x rate. The rate is `PlaybackOutput::speed`, which the page binds to the same `page.speed` as the `MediaPlayer`'s `playbackRate`.
- **Cap:** the estimate advances for at most `cap` of wall time after the last report. The cap is the median of the last 9 report intervals x 1.2, clamped to 20-150 ms; 120 ms until two reports in a row have given an interval. Steady 100 ms reports (Qt 6.4) give 120 ms, 50 ms reports (Qt 6.7) 60 ms. Intervals across a seek, a pause, a rate change or a jump of more than 250 ms do not count.
- **Hold window and no step back:** the hold window is max(40 ms, cap x rate) of video (120 ms at 1x with 100 ms reports). A report behind the estimate by at most the window holds the estimate until the report has caught up. The window of a report never shrinks inside it: it is the larger of the window before and after the report's own interval, the window from before a hold (so a rate decrease keeps the old rate's window until the estimate has caught up), and, while the last report held the estimate, that report's window. What that guarantees: **no step back unless a report is more than the window behind the estimate (which, apart from a rate change, means the media clock itself went backwards) or follows a seek; pausing returns to the last report.** So a lagging media clock does not send the stroker back to a point it already passed, nor the move to the next point again. After a step back the output behaves as with the script clock (more than 250 ms back eases in, less sends the earlier point again, `seekEasesIn`). The limits of the estimate (the duration, the loop end) never take it below what was already sent, so moving the loop end under the playhead holds it there.
- **Not playing** (pause, stop, end of media): the estimate is the last report and does not move. After a start it moves only from the first report on, so the soft start and the first move still begin at a report, as before.
- **Hold** (frozen where it is, moving again from the next report): a rate change, an offset change (the offset is still applied once, to the estimated video time, PS-01; the change is no report, so it neither feeds the stale watchdog nor wakes the output while the video plays), and the emergency stop. **Seek** (frozen, and the next report taken as it is if it comes within the cap; a seek without a report expires): every seek of the page that moves the video (`seekTo`, `seekBy`: the transport, the timeline, the keys, chapters and bookmarks call `PlayerController::holdMediaPlayhead()` before they set the position; a seek to where the video already is announces nothing) and the report that reaches the end of a loop (the page jumps back right after it). With the loop on, the estimate stops 1 ms before the loop end, so only a report reaches it. A new video clears the measured interval.
- **Tick:** a 16 ms `Qt::PreciseTimer` (the script clock's rate) that runs only while the video plays. It emits only while the estimate advances (within the cap) and only when the millisecond value changed. It feeds only the output: the curves, the heatmap and the automatic Extended-O still follow the reports.
- **Safety:** the ticks call `PlaybackOutput::update(t, false)`. Such a call neither restarts the stale watchdog nor starts the output again after a stop. So the watchdog still stops the device 1.5 s after the last *report*, also while the estimate moves. A pause stops the ticks first, then stops the device at once, as before. The emergency stop is unchanged (a halted output ignores every position) and also freezes the estimate.
- **How far the device may run ahead of the picture:** at most the hold window in force (`HoldWindowMs()`) ahead of the latest report: max(40 ms, cap x rate) of video in steady play, at most 150 ms at 1x and 300 ms at 2x (120 ms with steady 100 ms reports at 1x); after a rate decrease, until the video has caught up, the window of the old rate (at most 600 ms after 4x). In steady play the estimate stays within 2 ms of the media clock (unit test). When the media clock stalls, the device runs on for at most the cap and then stands; when the reports resume behind the estimate by up to the window, it waits for them, further behind it steps back to them (unit tests of the bound with hiccups and stalls at 0.5x, 1x and 2x, and a random-play test with rate changes, holds, seeks, stalls and rewinds).
- **Tests:** `tests/core/playhead_test.cpp` (22 cases; the second review added: a rate decrease keeps the old window, a new interval does not shrink the window of its report, a seek without a report expires, a stalled clock that keeps reporting one position, and random play with rate changes, holds, seeks, stalls and rewinds (3000 runs; no step back that is not allowed, never further ahead than the window); the first 17: rate 1, 2 and 0.5 within 2 ms; the cap; no advance before the first report; pause and stop; a 300 ms stall stops at the cap; no step back for a report up to the window behind, at 1x and 4x; further behind is a seek; `Seek()` takes the next report as it is; seeks either way; loop jumps; rate change; hold; noisy reports never go backwards; the bound). `playback_output_test`: `videoStrokeStartsBetweenReports`, `videoLevelFollowsTheEstimate`, `videoPlayheadFollowsThePlaySpeed`, `videoPauseStopsAtOnceAndTheTicksEnd`, `videoEmergencyStopSilencesTheTicks`, `videoTicksDoNotKeepTheWatchdogAlive`, `scriptClockHasNoVideoPlayhead`, `videoOffsetAndLoopJumpHoldTheEstimate`, `videoReportBehindTheEstimateSendsNoMoveTwice`, `videoSeekOfThePageTakesTheReportAsItIs`, `videoTicksStopBeforeTheLoopEnd`, `videoOffsetChangeDoesNotFeedTheWatchdog`, `videoLoopEndBelowThePlayheadDoesNotStepBack`; `player_page_qml_test` (`seekTo` and `seekBy` announce a seek, not one to the current position).

### 7.2 Measured (5 and 6 Nov 2026)

Same setup as § 4 (4 cores, shared VM, Linux 6.18, Qt 6.4.2, offscreen, FFmpeg backend, unoptimised build), the video scenarios of `timing_bench`: 5 plays of the 4 s clip into the UDP TCode stand-in, three repetitions each. **Before:** the unchanged code, 5 Nov 20:33-20:36 UTC, loadavg (1 min) at the start of the scenarios 1.58-3.19. **After:** this branch with the review fixes of PR #163, 6 Nov 07:52-07:57 UTC, loadavg 1.35-2.23, with the report-only path (`media raw`) run in the same repetitions for comparison. Medians of the three repetitions' p50 / p95 / p99, and the extremes of all three. All rows, every repetition's p99, the minimums and the loadavg ends: [`benchmarks/TIMING_PLAYHEAD_2026-11-06.md`](benchmarks/TIMING_PLAYHEAD_2026-11-06.md) (the first "after" runs of 5 Nov, before the review fixes, with the same picture: [`benchmarks/TIMING_PLAYHEAD_2026-11-05.md`](benchmarks/TIMING_PLAYHEAD_2026-11-05.md)).

| Scenario | move start - due p50 / p95 / p99 / max | move end - due min / p50 / p99 / max | move shorter than scripted p50 / p99 / max | duration - formula | extra sends within a move |
|---|---|---|---|---|---|
| Before, video | 43.2 / 89.8 / 89.8 / 96.9 | -2.0 / 0.49 / 5.0 / 8.9 | not measured (older bench) | 0 in 120 moves | not counted |
| Before, video +load3 | 41.4 / 89.0 / 89.6 / 89.6 | -92.0 / 0.08 / 3.2 / 12.8 | not measured | 0 in 120 moves | not counted |
| After, reports only (`media raw`) | 41.6 / 89.7 / 90.3 / 91.1 | -0.2 / 0.35 / 1.7 / 3.0 | 9.4 % / 20.4 % / 20.4 % | 0 in 120 moves | 0 |
| After, reports only, +load3 | 42.6 / 88.9 / 89.4 / 93.5 | -2.6 / 0.21 / 3.6 / 4.6 | 9.4 % / 20.4 % / 20.4 % | 0 in 120 moves | 0 |
| **After, reports + estimate (`media`)** | **4.5 / 12.5 / 13.4 / 16.0** | -1.1 / 0.26 / 3.2 / 4.4 | **1.2 % / 3.0 % / 3.2 %** | 0 in 120 moves | **0** |
| **After, reports + estimate, +load3** | **4.6 / 12.7 / 14.0 / 17.2** | -2.1 / 0.13 / 3.7 / 4.9 | **1.4 % / 3.0 % / 4.2 %** | 0 in 120 moves | **0** |

- **Stroke start:** from 42 ms (p50) and 90 ms (p95 / p99) late to 4.5 ms (p50) and 13-14 ms (p99), max 17.2 ms. That is at or below the script clock's 8-9 ms p50 / 16-17 ms p99 (4.2), because a move can now go out at a tick or at a report, whichever comes first. The goal (about the script clock's figure, at most about 20 ms p99) is met.
- **Squeeze:** a move is now at most 4.2 % shorter than scripted (reports only: up to 20.4 % on this script; 29 % is the worst case of § 5 finding 3).
- **No move twice:** no move was sent again and none reversed within a move, in 480 moves of the "after" runs. The media clock fell about 100 ms behind a steady clock at the start of some plays under load (row "media clock behind a linear clock", max 102 ms), so the hold rule was exercised; whether the clean test clip ever produced a report 41-250 ms behind the estimate (the case of the review) was not checked; the unit and app tests cover that case.
- **Stroke end and the formula:** unchanged. The sent duration equals `max(20, (at - t) / speed)` in all 480 moves of the "after" runs. The "due" times follow the media clock's own reports, so a hiccup of the media clock shows up as a move end error in both paths: -92 ms once in the "before" runs; in the first "after" runs of 5 Nov one move end of +44.6 ms where the media clock fell 44 ms behind once.
- **`PlaybackOutput::update` at a report** (the bench times only those calls) takes 14 us at p50 with the estimate, where a tick has often sent the change already, and 32 us with reports only.
- **Levels** were not measured on the video path; the bench's video scenario drives a stroker. They take the same estimate (`videoLevelFollowsTheEstimate`).

### 7.3 Not verified

- **Windows, Qt 6.7.3** (the release): its FFmpeg backend reports every 50 ms, so the cap becomes 60 ms. The gain should be smaller (before: up to 50 ms late); the tick dominates either way. Not run.
- **A real device,** the GStreamer backend, a GUI thread that also renders QML and video frames, and long or real videos (4 s test pattern only).
- **A media clock that runs slower than the wall clock for a long time.** Each report would hold the estimate up to 40 ms, or step back. Not seen in these runs.
