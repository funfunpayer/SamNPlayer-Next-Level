//go:build !windows

package videox

import "os/exec"

// hideConsoleWindow is a no-op off Windows (child processes never flash a console).
func hideConsoleWindow(cmd *exec.Cmd) {}
