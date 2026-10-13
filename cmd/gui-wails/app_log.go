package main

import (
	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// GetLogEntries returns the in-memory log ring for the Log tab (copyable).
func (a *App) GetLogEntries() []logging.Entry {
	return logging.Recent()
}

// ClearLogEntries clears the in-memory ring only (log file stays).
func (a *App) ClearLogEntries() {
	logging.ClearRecent()
}

func (a *App) registerLogLiveHook() {
	logging.SetLiveHook(func(e logging.Entry) {
		if a.ctx == nil {
			return
		}
		runtime.EventsEmit(a.ctx, "log:line", map[string]any{
			"time":    e.Time.Format("15:04:05.000"),
			"level":   e.Level,
			"message": e.Message,
		})
	})
}
