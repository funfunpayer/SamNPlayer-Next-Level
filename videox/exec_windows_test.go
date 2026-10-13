//go:build windows

package videox

import (
	"context"
	"testing"
)

func TestCommandContextHidesConsoleOnWindows(t *testing.T) {
	ResetToolCache()
	if !Available() {
		t.Skip("ffmpeg not available")
	}
	cmd, err := CommandContext(context.Background(), "-version")
	if err != nil {
		t.Fatal(err)
	}
	if cmd.SysProcAttr == nil || !cmd.SysProcAttr.HideWindow {
		t.Fatal("Windows ffmpeg spawn must set HideWindow to avoid console flash popup")
	}
}
