package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRuntimeDirsIncludeSamNPlayerTree(t *testing.T) {
	dirs := runtimeDirs()
	if len(dirs) == 0 {
		t.Fatal("erwarte mindestens Config-/Cache-Ordner")
	}
	foundLogs := false
	for _, d := range dirs {
		if filepath.Base(d) == "sessions" || filepath.Base(d) == "logs" {
			foundLogs = true
		}
		if d == "" {
			t.Fatal("leerer Pfad in runtimeDirs")
		}
	}
	if !foundLogs {
		t.Fatalf("erwarte logs/sessions unter SamNPlayer, bekam %v", dirs)
	}
}

func TestEnsureRuntimeReadyCreatesMissingDirs(t *testing.T) {
	base := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", base)
	t.Setenv("HOME", base)
	// UserConfigDir auf Linux nutzt XDG_CONFIG_HOME.
	a := NewApp()
	h := a.ensureRuntimeReady()
	want := filepath.Join(base, "SamNPlayer", "logs", "sessions")
	if _, err := os.Stat(want); err != nil {
		t.Fatalf("sessions-Ordner fehlt nach ensureRuntimeReady: %v (health=%+v)", err, h)
	}
	depsOK := false
	for _, d := range h.Deps {
		if d.ID == "ffmpeg" {
			depsOK = true
			break
		}
	}
	if !depsOK {
		t.Fatalf("erwarte ffmpeg in Deps: %+v", h.Deps)
	}
}

func TestMaybeConnectSmokeTestRespectsSetting(t *testing.T) {
	a := NewApp()
	_ = a.settings.Set(prefDeviceConnectTest, false)
	dev := &countingDevice{}
	a.maybeConnectSmokeTest(dev)
	if dev.vib != 0 || dev.suc != 0 {
		t.Fatalf("bei ausgeschaltetem Test keine Impulse erwartet, vib=%d suc=%d", dev.vib, dev.suc)
	}

	_ = a.settings.Set(prefDeviceConnectTest, true)
	a.maybeConnectSmokeTest(dev)
	if dev.vib == 0 || dev.suc == 0 || !dev.stopped {
		t.Fatalf("bei eingeschaltetem Test Vib/Sog/Stop erwartet, vib=%d suc=%d stop=%v",
			dev.vib, dev.suc, dev.stopped)
	}
}
