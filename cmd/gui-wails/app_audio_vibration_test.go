package main

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func stubAudioVibration(t *testing.T, fn func(ctx context.Context, path string, stepMs int64) ([]float64, error)) {
	t.Helper()
	old := audioVibrationEnvelopeFn
	audioVibrationEnvelopeFn = fn
	t.Cleanup(func() { audioVibrationEnvelopeFn = old })
}

func TestAudioVibrationSettingSwapsVibrationOnly(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	video := filepath.Join(t.TempDir(), "clip.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	calls := 0
	stubAudioVibration(t, func(_ context.Context, path string, stepMs int64) ([]float64, error) {
		calls++
		if path != video || stepMs != 50 {
			t.Fatalf("decode %q step %d", path, stepMs)
		}
		return []float64{0.2, 0.7}, nil
	})
	a := NewApp()
	a.stateMu.Lock()
	a.setVideoPathLocked(video)
	a.stateMu.Unlock()
	frames := []funscript.Frame{{At: 0, Vibration: 0.9, Suction: 0.3}, {At: 50, Vibration: 0.9, Suction: 0.6}}

	if a.GetSettings().PlaybackAudioVibration {
		t.Fatal("default must be off (no silent feel change)")
	}
	if out, note := a.withAudioVibration(frames, 50, 1); note != "" || out[0].Vibration != 0.9 || calls != 0 {
		t.Fatalf("off: script vibration must stay and nothing decoded; note %q calls %d", note, calls)
	}

	if err := a.SetSetting(prefPlaybackAudioVibration, true); err != nil {
		t.Fatal(err)
	}
	if !a.GetSettings().PlaybackAudioVibration {
		t.Fatal("setting not persisted")
	}
	out, note := a.withAudioVibration(frames, 50, 1)
	if out[0].Vibration != 0.2 || out[1].Vibration != 0.7 {
		t.Fatalf("on: vibration must follow audio, got %+v", out)
	}
	if out[0].Suction != 0.3 || out[1].Suction != 0.6 {
		t.Fatalf("on: suction must follow script, got %+v", out)
	}
	if !strings.Contains(note, "audio") {
		t.Fatalf("Play log note missing: %q", note)
	}
	a.withAudioVibration(frames, 50, 1)
	if calls != 1 {
		t.Fatalf("envelope must be cached per video (SetSetting warm-up + 2 plays), decoded %d times", calls)
	}
}

func TestAudioVibrationFallsBackToScript(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	video := filepath.Join(t.TempDir(), "silent.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	stubAudioVibration(t, func(context.Context, string, int64) ([]float64, error) {
		return nil, errors.New("Keine Audiospur lesbar")
	})
	a := NewApp()
	if err := a.settings.Set(prefPlaybackAudioVibration, true); err != nil {
		t.Fatal(err)
	}
	frames := []funscript.Frame{{At: 0, Vibration: 0.4}}

	out, note := a.withAudioVibration(frames, 50, 1)
	if out[0].Vibration != 0.4 || !strings.Contains(note, "no video") {
		t.Fatalf("no video: keep script vib + say so; got %v %q", out[0].Vibration, note)
	}
	a.stateMu.Lock()
	a.setVideoPathLocked(video)
	a.stateMu.Unlock()
	out, note = a.withAudioVibration(frames, 50, 1)
	if out[0].Vibration != 0.4 || !strings.Contains(note, "unavailable") {
		t.Fatalf("no audio: keep script vib + say so; got %v %q", out[0].Vibration, note)
	}
}
