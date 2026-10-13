package main

import (
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/player"
)

// Rejected StartTraining must not overwrite the live session's device pointer.
func TestStartTrainingKeepsDeviceWhenSessionBusy(t *testing.T) {
	a := NewApp()
	keep := device.NewMock(false)
	a.stateMu.Lock()
	a.activeDevice = keep
	a.sessionActive = true
	a.stateMu.Unlock()

	err := a.StartTraining(TrainingRequest{
		Mock: true, Cycles: 1, Technique: "stopstart", Channel: "vibration",
		RampUpMs: 100, HoldMs: 100, RestMs: 100, PeakIntensity: 0.5,
	})
	if err == nil {
		t.Fatal("expected busy session error")
	}
	if !strings.Contains(err.Error(), "already running") {
		t.Fatalf("unexpected error: %v", err)
	}
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	if a.activeDevice != keep {
		t.Fatal("activeDevice was replaced on rejected StartTraining")
	}
}

// Rejected StartPlayback must not replace the live session's player.
func TestStartPlaybackKeepsPlayerWhenSessionBusy(t *testing.T) {
	a := NewApp()
	keep := player.New(device.NewMock(false))
	a.stateMu.Lock()
	a.activePlayer = keep
	a.activeDevice = keep.Device
	a.sessionActive = true
	a.currentScript = &funscript.Script{Actions: []funscript.Action{
		{At: 0, Pos: 0}, {At: 1000, Pos: 100}, {At: 2000, Pos: 0},
	}}
	a.stateMu.Unlock()

	err := a.StartPlayback(PlaybackOptions{Mock: true})
	if err == nil {
		t.Fatal("expected busy session error")
	}
	if !strings.Contains(err.Error(), "already running") {
		t.Fatalf("unexpected error: %v", err)
	}
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	if a.activePlayer != keep {
		t.Fatal("activePlayer was replaced on rejected StartPlayback")
	}
}
