//go:build !windows

package update

import (
	"fmt"
	"os/exec"
	"syscall"
)

// detachedSysProcAttr startet den Hilfsprozess in einer eigenen Session,
// damit er unabhängig vom (gleich per os.Exit beendeten) Hauptprozess
// weiterläuft.
func detachedSysProcAttr() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{
		Setsid: true,
	}
}

// startApplyHelper waits for pid to exit, replaces self with newPath, then
// execs the new binary. Runs detached so os.Exit in the parent does not kill it.
func startApplyHelper(pid int, newPath, self string) error {
	script := fmt.Sprintf(
		`while kill -0 %d 2>/dev/null; do sleep 0.2; done; mv -f "%s" "%s"; chmod +x "%s"; exec "%s"`,
		pid, newPath, self, self, self,
	)
	cmd := exec.Command("sh", "-c", script)
	cmd.SysProcAttr = detachedSysProcAttr()
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("update: helper process could not be started: %w", err)
	}
	return nil
}
