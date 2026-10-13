package update

import (
	"fmt"
	"strings"
)

// windowsApplyBatch builds a .cmd helper that waits for pid to exit, moves
// newPath over self, then restarts. Written to a temp file so we never pass
// `start "" "path"` through Go's Windows EscapeArg + `cmd /C` nesting —
// that mangled the empty window title and could surface a useless OS dialog
// (Owner saw a popup containing only "//") instead of restarting cleanly.
//
// Pure string builder — tested on all GOOS; only executed on Windows.
func windowsApplyBatch(pid int, newPath, self string) string {
	// Batch: % must be doubled; paths are quoted. No delayed expansion.
	return fmt.Sprintf(
		"@echo off\r\n"+
			"setlocal\r\n"+
			":wait\r\n"+
			"tasklist /FI \"PID eq %d\" 2>nul | find \"%d\" >nul\r\n"+
			"if not errorlevel 1 (\r\n"+
			"  ping 127.0.0.1 -n 2 >nul\r\n"+
			"  goto wait\r\n"+
			")\r\n"+
			"move /Y \"%s\" \"%s\"\r\n"+
			"if errorlevel 1 exit /b 1\r\n"+
			"start \"\" \"%s\"\r\n"+
			"del \"%%~f0\"\r\n",
		pid, pid,
		escapeBatchPath(newPath),
		escapeBatchPath(self),
		escapeBatchPath(self),
	)
}

func escapeBatchPath(p string) string {
	// Paths from os.Executable / CreateTemp should not contain ", but be safe.
	return strings.ReplaceAll(p, `"`, `""`)
}
