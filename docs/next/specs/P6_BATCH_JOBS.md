# P6 — Smart Batch: job states and on-disk checkpoints

**Status:** spec, not built · input to STU-01 / STUDIO-A6 (gate N3) · 12 Oct 2026

## Why

Free has a Generate queue that lives only in the frontend
(`generator.js`: `generateQueue = [{path, status}]`, statuses
`queued|running|done|failed|skipped`). It is lost on restart, cannot pause, and
has no report. Next's first sellable feature is a batch that survives crashes
and tells the user exactly what happened per clip.

## Rules

1. **The queue lives in Go**, not in the UI. The UI only shows and sends commands.
2. **Batch runs the same single-clip pipeline** as Create. No second generator.
3. **Nothing is lost on crash.** Every state change is on disk before it is
   reported. Restart resumes; it never repeats a finished clip.
4. **Never overwrite** existing outputs unless the batch was created with
   `overwrite: true`.
5. **One clip failing never stops the batch.**
6. **Safety:** a batch never starts or drives a device.

## States

```text
batch:  draft -> running <-> paused -> done
                   \-> cancelled
clip:   queued -> running -> verifying -> done
                     |          \-> review   (quality gate failed, output kept, user decides)
                     \-> failed (error, retry count < max -> queued)
        queued|running -> cancelled
        any -> skipped (input missing/unchanged output exists and overwrite=false)
```

`review` is a finished state: output written, marked for the user. `failed`
with retries left goes back to `queued`; at the limit it stays `failed`.

## On disk

One folder per batch under the app data dir:
`batches/<batch-id>/`

| File | Content | Written |
|---|---|---|
| `batch.json` | Settings: preset, output dir, overwrite, max retries, parallel jobs, created_at | Once, at creation |
| `journal.jsonl` | Append-only events: `{seq, at, clip, from, to, reason}` | Each transition, fsync |
| `clips/<clip-id>.json` | Input path, input fingerprint (size + mtime + sha256 of first MiB, as Free `sceneMap.video.sha256_head`), pipeline version, outputs, quality summary, error | On each clip state change, atomic rename |
| `report.json` / `report.csv` | Per-clip result table | On `done`/`cancelled`, rebuilt any time from the journal |

The journal is the source of truth; `clips/*.json` are a cache that can be
rebuilt from it.

## Resume after crash

1. Replay `journal.jsonl` up to the last complete line (a torn last line is
   ignored).
2. Clips left in `running` or `verifying` go back to `queued`. Their partial
   outputs are temp files (never the final name), so they are deleted.
3. A clip whose input fingerprint changed since it was queued is re-queued
   with reason `input_changed`, even if it was `done`.
4. Batch state returns to `paused`; the user presses Resume. (No surprise CPU
   load after a crash.)

## Resources

- `parallel_jobs` default 1; max = CPU cores / 4, at least 1.
- One GPU slot, exclusive. A job needing GPU waits for it.
- One video decoder per job; no decoder pool fan-out.
- Optional "low priority" (OS process priority) for background running.

## Go API (Wails bindings)

```go
CreateBatch(settings BatchSettings, inputs []string) (BatchID, error)
StartBatch(id) / PauseBatch(id) / ResumeBatch(id) / CancelBatch(id) error
RetryClips(id, clipIDs []string) error
GetBatch(id) (BatchView, error)
ListBatches() ([]BatchSummary, error)
// event: "batch:update" {batchID, clipID, state, progress}
```

Per-clip progress comes from executed pipeline steps, never from a timer.

## Acceptance (STU-02)

20 clips, including: VFR, no audio, broken file, 1-second clip, Unicode file
name, read-only output folder, duplicate input path, input deleted mid-batch.

- Kill the process at random points 20 times → resume → final outputs identical
  to an uninterrupted run, no clip done twice, no orphan temp files.
- Pause/resume/cancel at every state.
- `overwrite: false` with existing outputs → `skipped`, files untouched.
- Report counts match the journal; CSV opens in a spreadsheet with Unicode names intact.
- Free single-clip Create output for the same clip is identical to the batch
  output (same pipeline).
