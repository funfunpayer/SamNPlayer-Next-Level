# samn.library

Finding scripts for videos: same name beside the video, script folders,
tolerant names and variants (Soft / Hard / authors).

- **API:** `find_script_for_video(video, folders) -> VideoScriptMatch`,
  `normalize_title`, `variant_label`, `split_folders`.
- **Depends on:** the standard library only.
- **Go reference:** `cmd/gui-wails/app_open_media.go` (tests ported 1:1).
- **Status:** P1 logic done; the Qt "Open…" flow comes next.
