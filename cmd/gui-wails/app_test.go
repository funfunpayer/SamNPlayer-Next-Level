package main

import (
	"context"
	"sync"
	"testing"
)

// fakeDevice records Stop()/Disconnect() calls for shutdown() tests below -
// device.Mock doesn't track call counts (it only optionally prints), so a
// small local fake is simpler than extending shared production code just
// for a test.
type fakeDevice struct {
	mu         sync.Mutex
	stopped    bool
	disconnect bool
}

func (f *fakeDevice) Connect(context.Context) error { return nil }
func (f *fakeDevice) SetVibration(float64) error    { return nil }
func (f *fakeDevice) SetSuction(float64) error      { return nil }
func (f *fakeDevice) Stop() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.stopped = true
	return nil
}
func (f *fakeDevice) Disconnect() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.disconnect = true
	return nil
}

// shutdown() previously only stopped/disconnected a.testDevice (the Geräte-
// Tab test connection) - it never touched a.activeDevice or cancelled
// a.playCancel. Closing the app window during a live playback/training
// session left the device running and the connection open indefinitely.
// This reproduces that gap directly against shutdown() (not a full
// StartPlayback, which needs a real/scan-capable device).
func TestShutdownStopsActiveSessionDevice(t *testing.T) {
	a := NewApp()
	dev := &fakeDevice{}
	cancelled := false

	a.stateMu.Lock()
	a.activeDevice = dev
	a.playCancel = func() { cancelled = true }
	a.stateMu.Unlock()

	a.shutdown(context.Background())

	dev.mu.Lock()
	defer dev.mu.Unlock()
	if !dev.stopped {
		t.Error("shutdown() hat das aktive Gerät nicht gestoppt")
	}
	if !dev.disconnect {
		t.Error("shutdown() hat das aktive Gerät nicht getrennt")
	}
	if !cancelled {
		t.Error("shutdown() hat die laufende Sitzung nicht abgebrochen (playCancel nicht aufgerufen)")
	}
}

// Ohne aktive Sitzung darf shutdown() nicht abstürzen oder sich anders
// verhalten - der bereits vorher abgedeckte Fall (nur testDevice).
func TestShutdownWithoutActiveSessionIsSafe(t *testing.T) {
	a := NewApp()
	a.shutdown(context.Background())
}
