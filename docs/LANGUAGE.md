# Product language

SamNPlayer’s **user-facing language is English**:

- GUI labels, buttons, hints, and status text
- Project documentation under `docs/` and the root README
- GitHub-facing text (release notes curated in `CHANGELOG.md`, PR descriptions)

## Why

Mixed German UI + English docs confused users and contributors. One
language keeps the product and the repository aligned.

## What stays as-is

- **Programming languages** stay where they fit: Go for the desktop shell,
  player, BLE, and packaging; Python for CV/training scripts; JS for the
  Wails UI. Do not rewrite stacks for language fashion — only when a
  measured win (performance, maintainability, or removing a dependency)
  justifies the cost.
- **Operator-facing logs and backend errors** (Log tab, stderr progress,
  `fmt.Errorf` returned to the GUI, `logging.Info`/`Warn`/`Error`) should be
  **English**. When you touch a log line, translate it; update structured log
  attribute keys to English when adjacent (`fehler`→`error`, `geraet`→`device`,
  `pfad`→`path`, `hinweis`→`hint`, `zeile`→`line`).
- **Code comments** may still be German in older files; new comments
  preferably English. Do not mass-rewrite comments for translation alone.
- **Brand / domain terms** stay: SamNPlayer, Funscript, Extended-O, Tf/Tj,
  CSRT, ONNX, OFS, etc.

## Contributing

When you add UI or docs, write English. If you find leftover German
user strings, replace them in the same change when practical.
