//go:build windows

package update

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
)

// detachedSysProcAttr sorgt dafür, dass der Hilfsprozess unabhängig vom
// aktuellen Prozess weiterläuft (kein Konsolenfenster, eigene Prozessgruppe),
// damit os.Exit() im Hauptprozess ihn nicht mit runterreißt.
func detachedSysProcAttr() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{
		CreationFlags: syscall.CREATE_NEW_PROCESS_GROUP | 0x08000000, // + CREATE_NO_WINDOW
	}
}

// startApplyHelper writes the helper .cmd and starts it detached.
func startApplyHelper(pid int, newPath, self string) error {
	bat := windowsApplyBatch(pid, newPath, self)
	dir := os.TempDir()
	name := filepath.Join(dir, fmt.Sprintf("SamNPlayer-update-helper-%d.cmd", pid))
	if err := os.WriteFile(name, []byte(bat), 0o700); err != nil {
		return fmt.Errorf("update: helper script could not be written: %w", err)
	}
	// Run the .cmd by path (one argv, no nested quotes through EscapeArg).
	cmd := exec.Command(name)
	cmd.SysProcAttr = detachedSysProcAttr()
	if err := cmd.Start(); err != nil {
		os.Remove(name)
		return fmt.Errorf("update: helper process could not be started: %w", err)
	}
	return nil
}
