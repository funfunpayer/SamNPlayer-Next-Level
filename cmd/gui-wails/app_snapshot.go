package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/videox"
	"github.com/wailsapp/wails/v2/pkg/runtime"
)

// snapshotCommandFn builds the ffmpeg run; swapped in tests.
var snapshotCommandFn = func(ctx context.Context, video string, atMs int64, out string) error {
	cmd, err := videox.CommandContext(ctx,
		"-y", "-hide_banner", "-loglevel", "error",
		"-ss", strconv.FormatFloat(float64(atMs)/1000, 'f', 3, 64),
		"-i", video, "-frames:v", "1", out)
	if err != nil {
		return err
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("ffmpeg: %v %s", err, strings.TrimSpace(string(b)))
	}
	return nil
}

// snapshotSavePathFn asks where to save; swapped in tests.
var snapshotSavePathFn = func(a *App, suggested string) (string, error) {
	return runtime.SaveFileDialog(a.ctx, runtime.SaveDialogOptions{
		Title:           "Save video frame",
		DefaultFilename: suggested,
		Filters:         []runtime.FileFilter{{DisplayName: "PNG image", Pattern: "*.png"}},
	})
}

// SaveVideoSnapshot saves the Player video frame at atMs as PNG (V1 video bar
// "snapshot"). Returns the saved path, "" when the dialog was cancelled.
// The frame is decoded by ffmpeg from the original file, so it works for
// any codec the app can open (the webview cannot export a cross-origin frame).
func (a *App) SaveVideoSnapshot(atMs int64) (string, error) {
	video := a.currentVideoPath()
	if video == "" {
		return "", fmt.Errorf("no video linked")
	}
	if atMs < 0 {
		atMs = 0
	}
	base := strings.TrimSuffix(filepath.Base(video), filepath.Ext(video))
	sec := atMs / 1000
	suggested := fmt.Sprintf("%s_%02d-%02d.png", base, sec/60, sec%60)
	out, err := snapshotSavePathFn(a, suggested)
	if err != nil || out == "" {
		return "", err
	}
	if !strings.EqualFold(filepath.Ext(out), ".png") {
		out += ".png"
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	if err := snapshotCommandFn(ctx, video, atMs, out); err != nil {
		return "", err
	}
	if st, err := os.Stat(out); err != nil || st.Size() == 0 {
		return "", fmt.Errorf("snapshot not written")
	}
	return out, nil
}
