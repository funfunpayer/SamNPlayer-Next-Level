package player

import (
	"context"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func lastOut(d *recordingDev) (float64, float64) {
	vib, suc := d.snapshot()
	if len(vib) == 0 || len(suc) == 0 {
		return -1, -1
	}
	return vib[len(vib)-1], suc[len(suc)-1]
}

// Video pausiert (native Steuerung/Leertaste): das Frontend schickt keine
// Positionen mehr. Vorher blieb das Gerät auf dem letzten Wert stehen, bis
// weitergespielt oder gestoppt wurde. Jetzt geht es nach syncStaleAfter
// auf 0 und folgt bei der nächsten Position wieder dem Skript.
func TestSyncGoesQuietWhenPositionsStop(t *testing.T) {
	old := syncStaleAfter
	syncStaleAfter = 150 * time.Millisecond
	defer func() { syncStaleAfter = old }()

	dev := &recordingDev{}
	p := New(dev)
	frames := []funscript.Frame{{At: 0, Vibration: 0.8, Suction: 0.6}, {At: 10000, Vibration: 0.8, Suction: 0.6}}
	positions := make(chan int64, 4)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- p.Sync(ctx, frames, positions) }()

	positions <- 1000
	time.Sleep(50 * time.Millisecond)
	if v, s := lastOut(dev); v != 0.8 || s != 0.6 {
		t.Fatalf("läuft nicht: vib %v suc %v", v, s)
	}
	time.Sleep(300 * time.Millisecond) // "Video pausiert"
	if v, s := lastOut(dev); v != 0 || s != 0 {
		t.Errorf("ohne Positionen muss das Gerät auf 0: vib %v suc %v", v, s)
	}
	positions <- 1300 // weiter
	time.Sleep(50 * time.Millisecond)
	if v, s := lastOut(dev); v != 0.8 || s != 0.6 {
		t.Errorf("nach neuer Position nicht zurück: vib %v suc %v", v, s)
	}
	close(positions)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

// Frontend meldet Pause/Buffering sofort per SyncIdleSentinel — Gerät auf 0
// ohne syncStaleAfter abzuwarten.
func TestSyncGoesQuietOnIdleSentinel(t *testing.T) {
	old := syncStaleAfter
	syncStaleAfter = 5 * time.Second // would be too slow if sentinel ignored
	defer func() { syncStaleAfter = old }()

	dev := &recordingDev{}
	p := New(dev)
	frames := []funscript.Frame{{At: 0, Vibration: 0.8, Suction: 0.6}, {At: 10000, Vibration: 0.8, Suction: 0.6}}
	positions := make(chan int64, 4)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- p.Sync(ctx, frames, positions) }()

	positions <- 1000
	time.Sleep(40 * time.Millisecond)
	if v, s := lastOut(dev); v != 0.8 || s != 0.6 {
		t.Fatalf("läuft nicht: vib %v suc %v", v, s)
	}
	positions <- SyncIdleSentinel
	time.Sleep(40 * time.Millisecond)
	if v, s := lastOut(dev); v != 0 || s != 0 {
		t.Errorf("Idle-Sentinel muss Gerät sofort auf 0: vib %v suc %v", v, s)
	}
	positions <- 1500
	time.Sleep(40 * time.Millisecond)
	if v, s := lastOut(dev); v != 0.8 || s != 0.6 {
		t.Errorf("nach Resume nicht zurück: vib %v suc %v", v, s)
	}
	close(positions)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

// Pause during soft-start must zero immediately — goQuiet alone no-ops while
// firstFrame/quiet is still set, and soft-start used to block the positions
// channel so the idle sentinel was dropped until the ramp finished.
func TestSyncIdleDuringSoftStartGoesQuiet(t *testing.T) {
	old := syncStaleAfter
	syncStaleAfter = 5 * time.Second
	defer func() { syncStaleAfter = old }()

	dev := &recordingDev{}
	p := New(dev)
	p.SoftStartMs = 400 // 10 steps × 40ms
	frames := []funscript.Frame{{At: 0, Vibration: 1.0, Suction: 1.0}, {At: 10000, Vibration: 1.0, Suction: 1.0}}
	positions := make(chan int64, 4)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- p.Sync(ctx, frames, positions) }()

	positions <- 500
	// Let soft-start write at least one mid-ramp step.
	deadline := time.Now().Add(200 * time.Millisecond)
	for {
		vib, _ := lastOut(dev)
		if vib > 0 && vib < 1.0 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("soft-start did not ramp: last vib=%v", vib)
		}
		time.Sleep(5 * time.Millisecond)
	}
	positions <- SyncIdleSentinel
	deadline = time.Now().Add(150 * time.Millisecond)
	for {
		vib, suc := lastOut(dev)
		if vib == 0 && suc == 0 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("idle during soft-start must zero device: vib %v suc %v", vib, suc)
		}
		time.Sleep(5 * time.Millisecond)
	}
	// Resume must soft-start again (quiet path), not jump to full intensity.
	positions <- 800
	deadline = time.Now().Add(200 * time.Millisecond)
	sawRamp := false
	for {
		vib, _ := lastOut(dev)
		if vib > 0 && vib < 1.0 {
			sawRamp = true
			break
		}
		if vib == 1.0 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("no output after resume: vib=%v", vib)
		}
		time.Sleep(5 * time.Millisecond)
	}
	if !sawRamp {
		t.Errorf("resume after idle should soft-start again, got full intensity immediately")
	}
	close(positions)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}
