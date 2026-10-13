package main

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// FREE-04: the Free release chain without a GUI. A funscript from another
// app → Load → Save as Emotion Script (.samn) → load that again → Play on
// the mock device (frames reach the device path) → Stop mid-run → Export a
// .funscript for other apps that matches the original curve.

// eventRecorder replaces eventsEmit and keeps what the GUI would receive.
type eventRecorder struct {
	mu     sync.Mutex
	frames []map[string]any
	logs   []string
	errs   []string
	done   chan map[string]any
}

func recordEvents(t *testing.T) *eventRecorder {
	t.Helper()
	r := &eventRecorder{done: make(chan map[string]any, 4)}
	prev := eventsEmit
	eventsEmit = r.emit
	t.Cleanup(func() { eventsEmit = prev })
	return r
}

func (r *eventRecorder) emit(_ context.Context, name string, data ...interface{}) {
	r.mu.Lock()
	defer r.mu.Unlock()
	var payload any
	if len(data) > 0 {
		payload = data[0]
	}
	switch name {
	case "playback:frame":
		if m, ok := payload.(map[string]any); ok {
			r.frames = append(r.frames, m)
		}
	case "playback:log":
		if s, ok := payload.(string); ok {
			r.logs = append(r.logs, s)
		}
	case "playback:error":
		if s, ok := payload.(string); ok {
			r.errs = append(r.errs, s)
		}
	case "playback:done":
		m, _ := payload.(map[string]any)
		r.done <- m
	}
}

func (r *eventRecorder) waitDone(t *testing.T, timeout time.Duration) map[string]any {
	t.Helper()
	select {
	case m := <-r.done:
		return m
	case <-time.After(timeout):
		t.Fatalf("playback did not finish within %v", timeout)
		return nil
	}
}

func (r *eventRecorder) snapshot() (frames []map[string]any, errs []string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]map[string]any(nil), r.frames...), append([]string(nil), r.errs...)
}

// writeImportedFunscript writes a plain funscript as another app would: a
// steady 2 Hz stroke for durMs.
func writeImportedFunscript(t *testing.T, dir string, durMs int64) (string, []funscript.Action) {
	t.Helper()
	var acts []funscript.Action
	for at := int64(0); at <= durMs; at += 250 {
		pos := 10
		if (at/250)%2 == 1 {
			pos = 90
		}
		acts = append(acts, funscript.Action{At: at, Pos: pos})
	}
	body, err := json.Marshal(map[string]any{"version": "1.0", "inverted": false, "range": 100, "actions": acts})
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "imported clip.funscript")
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	return path, acts
}

// guiPlayOptions mirrors what the Player sends on Play with default settings
// (playback.js: tick 50, Max-Speed 0.6, independent, contact intensity 1).
// ContactIntensityScale must be 1: 0 means "mute vibration" on purpose.
func guiPlayOptions() PlaybackOptions {
	return PlaybackOptions{Mock: true, TickMs: 50, MaxSpeed: 0.6, SyncMode: "independent", ContactIntensityScale: 1}
}

// playToEnd plays the loaded script on the mock device and returns the
// frames the GUI saw. It fails the test on any playback error.
func playToEnd(t *testing.T, a *App, rec *eventRecorder, timeout time.Duration) []map[string]any {
	t.Helper()
	if err := a.StartPlayback(guiPlayOptions()); err != nil {
		t.Fatalf("StartPlayback: %v", err)
	}
	done := rec.waitDone(t, timeout)
	frames, errs := rec.snapshot()
	if failed, _ := done["failed"].(bool); failed || len(errs) > 0 {
		t.Fatalf("playback failed: %v %v", done, errs)
	}
	return frames
}

func TestFreeChainImportSaveReloadPlayExport(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	dir := t.TempDir()
	const durMs = 2000
	src, original := writeImportedFunscript(t, dir, durMs)

	// 1. Import a funscript from another app.
	a := NewApp()
	info, err := a.LoadFunscript(src)
	if err != nil {
		t.Fatalf("LoadFunscript(funscript): %v", err)
	}
	if info.ActionCount != len(original) || info.NativeFormat {
		t.Fatalf("imported info: %+v (want %d actions, not .samn)", info, len(original))
	}

	// 2. Save as Emotion Script and load that in a fresh app (restart).
	samnPath, err := a.SaveLoadedAsSamn()
	if err != nil {
		t.Fatalf("SaveLoadedAsSamn: %v", err)
	}
	if filepath.Ext(samnPath) != ".samn" {
		t.Fatalf("saved path %q is not .samn", samnPath)
	}
	b := NewApp()
	info2, err := b.LoadFunscript(samnPath)
	if err != nil {
		t.Fatalf("LoadFunscript(samn): %v", err)
	}
	if !info2.NativeFormat || info2.ActionCount != len(original) || info2.DurationMs != info.DurationMs {
		t.Fatalf("reloaded .samn differs: %+v vs %+v", info2, info)
	}

	// 3. Play on the mock device to the end.
	rec := recordEvents(t)
	frames := playToEnd(t, b, rec, 15*time.Second)
	if len(frames) < 20 {
		t.Fatalf("too few frames reached the device: %d", len(frames))
	}
	var last float64 = -1
	vibOn, sucOn := 0, 0
	for _, f := range frames {
		at, _ := f["atMs"].(int64)
		if float64(at) < last {
			t.Fatalf("frames went back in time: %v after %v", at, last)
		}
		last = float64(at)
		v, _ := f["vibration"].(float64)
		s, _ := f["suction"].(float64)
		if v < 0 || v > 1 || s < 0 || s > 1 {
			t.Fatalf("output out of range: %v", f)
		}
		if v > 0.05 {
			vibOn++
		}
		if s > 0.05 {
			sucOn++
		}
	}
	if vibOn == 0 || sucOn == 0 {
		t.Fatalf("the stroke must drive both channels: %d vibration frames, %d suction frames", vibOn, sucOn)
	}
	if last < durMs-200 {
		t.Fatalf("playback stopped early: last frame at %v ms of %d", last, durMs)
	}

	// 4. Export for other apps: the stroke curve comes back unchanged.
	out := filepath.Join(dir, "export.funscript")
	got, err := b.ExportLoadedFunscript(out)
	if err != nil {
		t.Fatalf("ExportLoadedFunscript: %v", err)
	}
	exported, err := funscript.Load(got)
	if err != nil {
		t.Fatalf("exported file does not load: %v", err)
	}
	if len(exported.Actions) != len(original) {
		t.Fatalf("export has %d actions, want %d", len(exported.Actions), len(original))
	}
	for i, act := range exported.Actions {
		if act.At != original[i].At || act.Pos != original[i].Pos {
			t.Fatalf("action %d: got %+v want %+v", i, act, original[i])
		}
	}
}

// Stop mid-run ends the session cleanly, and the next Play starts again.
func TestFreeChainStopMidRunThenPlayAgain(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	src, _ := writeImportedFunscript(t, t.TempDir(), 20000)
	a := NewApp()
	if _, err := a.LoadFunscript(src); err != nil {
		t.Fatal(err)
	}
	rec := recordEvents(t)
	if err := a.StartPlayback(guiPlayOptions()); err != nil {
		t.Fatal(err)
	}
	time.Sleep(400 * time.Millisecond)
	start := time.Now()
	a.StopPlayback()
	rec.waitDone(t, 5*time.Second)
	if took := time.Since(start); took > 3*time.Second {
		t.Fatalf("Stop took %v", took)
	}
	frames, _ := rec.snapshot()
	if len(frames) == 0 {
		t.Fatal("no frames before Stop")
	}
	if at, _ := frames[len(frames)-1]["atMs"].(int64); at > 10000 {
		t.Fatalf("Stop did not stop: last frame at %d ms", at)
	}
	// Session slot is free again: a second Play is accepted.
	if err := a.StartPlayback(guiPlayOptions()); err != nil {
		t.Fatalf("second StartPlayback after Stop: %v", err)
	}
	a.StopPlayback()
	rec.waitDone(t, 5*time.Second)
}
