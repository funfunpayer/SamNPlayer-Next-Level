package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestAudioLevelEnvelopeSharesDecodeWithAudioVibration(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if env, err := a.GetAudioLevelEnvelope(); err != nil || len(env) != 0 {
		t.Fatalf("no video: want empty, got %v %v", env, err)
	}
	video := filepath.Join(t.TempDir(), "clip.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	calls := 0
	stubAudioVibration(t, func(_ context.Context, path string, stepMs int64) ([]float64, error) {
		calls++
		if path != video || stepMs != audioLevelStepMs {
			t.Fatalf("decode %q step %d", path, stepMs)
		}
		return []float64{0.1, 0.9, 0.4}, nil
	})
	a.stateMu.Lock()
	a.setVideoPathLocked(video)
	a.stateMu.Unlock()
	env, err := a.GetAudioLevelEnvelope()
	if err != nil || len(env) != 3 || env[1] != 0.9 {
		t.Fatalf("envelope %v err %v", env, err)
	}
	// Audio vibration at the default 50 ms tick reuses the cached decode.
	if _, err := a.audioVibrationEnvelope(context.Background(), video, 50); err != nil || calls != 1 {
		t.Fatalf("expected one shared decode, calls %d err %v", calls, err)
	}
}

func TestSetChannelLevelsStoresClampedCut(t *testing.T) {
	a := NewApp()
	a.SetChannelLevels(0.3, 2)
	a.stateMu.RLock()
	vib, suc := 1-a.vibCut, 1-a.sucCut
	a.stateMu.RUnlock()
	if vib < 0.299 || vib > 0.301 || suc != 1 {
		t.Fatalf("levels %v %v, want 0.3 1", vib, suc)
	}
}

func TestSetPeakReduceStored(t *testing.T) {
	a := NewApp()
	a.SetPeakReduce(true)
	a.stateMu.RLock()
	on := a.peakReduce
	a.stateMu.RUnlock()
	if !on {
		t.Fatal("SetPeakReduce(true) not stored for the next playback")
	}
}

func TestSetStrokeOutputStoresClampedRange(t *testing.T) {
	a := NewApp()
	got := a.SetStrokeOutput(false, 0.5, 0.52)
	if len(got) != 2 || got[1]-got[0] < 0.0999 {
		t.Fatalf("range %v, want at least 10 %% wide", got)
	}
	a.stateMu.RLock()
	off, lo, hi := a.strokeOff, a.strokeLo, 1-a.strokeHiCut
	a.stateMu.RUnlock()
	if !off || lo != got[0] || hi != got[1] {
		t.Fatalf("stored off=%v %v–%v, want off %v", off, lo, hi, got)
	}
}
