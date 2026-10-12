package main

import (
	"sync"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// Concurrent load + read of script state must not race under -race.
func TestLoadedScriptStateNoRace(t *testing.T) {
	a := NewApp()
	actions := []funscript.Action{{At: 0, Pos: 20}, {At: 400, Pos: 80}, {At: 800, Pos: 20}}
	var wg sync.WaitGroup
	for i := 0; i < 32; i++ {
		wg.Add(2)
		go func(n int) {
			defer wg.Done()
			script := &funscript.Script{Actions: actions}
			a.setLoadedScript("/tmp/race.funscript", script)
		}(i)
		go func() {
			defer wg.Done()
			_ = a.loadedScript()
			_ = a.loadedScriptPath()
			_, _ = a.GetScriptCurve(50)
			_, _ = a.AnalyzeScript()
		}()
	}
	wg.Wait()
}
