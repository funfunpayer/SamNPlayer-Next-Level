package main

import "github.com/funfunpayer/SamNPlayer/funscript"

// Script-state accessors — currentScript / currentFrames / scriptPath were
// read and written without stateMu while playback, analysis, and load ran
// concurrently (review finding). Keep them behind the same lock as
// currentScriptPath / videoPath.

func (a *App) setLoadedScript(path string, script *funscript.Script) {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	a.currentScript = script
	a.scriptPath = path
	a.currentScriptPath = path
	a.currentFrames = nil
	if path != "" && a.settings != nil {
		a.scriptOffsetMs = int64(a.settings.GetFloat(offsetKeyFor(path), 0))
	}
}

func (a *App) loadedScript() *funscript.Script {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	return a.currentScript
}

func (a *App) loadedScriptPath() string {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	return a.scriptPath
}

func (a *App) setCurrentFrames(frames []funscript.Frame) {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	a.currentFrames = frames
}

func (a *App) replaceLoadedActions(actions []funscript.Action) {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	if a.currentScript == nil {
		return
	}
	a.currentScript.Actions = actions
	a.currentFrames = nil
}
