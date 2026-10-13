//go:build windows

package videox

import (
	"os/exec"
	"syscall"
)

// hideConsoleWindow suppresses the brief black console flash Windows shows
// when a GUI app starts ffmpeg/ffprobe (console subsystem binaries).
func hideConsoleWindow(cmd *exec.Cmd) {
	if cmd == nil {
		return
	}
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
}
