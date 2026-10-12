//go:build !windows

package generator

import "syscall"

// hiddenSysProcAttr: unter Linux/macOS startet ein Kindprozess nie ein
// eigenes Konsolenfenster - nichts zu unterdrücken.
func hiddenSysProcAttr() *syscall.SysProcAttr {
	return nil
}
