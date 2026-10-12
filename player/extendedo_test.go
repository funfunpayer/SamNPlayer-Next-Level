package player

import (
	"context"
	"math"
	"sync"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// recordingDev speichert jeden SetVibration/SetSuction-Aufruf für Assertions.
type recordingDev struct {
	mu  sync.Mutex
	vib []float64
	suc []float64
}

func (d *recordingDev) Connect(context.Context) error { return nil }
func (d *recordingDev) Disconnect() error             { return nil }
func (d *recordingDev) Stop() error                   { return nil }
func (d *recordingDev) SetVibration(v float64) error {
	d.mu.Lock()
	d.vib = append(d.vib, v)
	d.mu.Unlock()
	return nil
}
func (d *recordingDev) SetSuction(s float64) error {
	d.mu.Lock()
	d.suc = append(d.suc, s)
	d.mu.Unlock()
	return nil
}
func (d *recordingDev) snapshot() (vib, suc []float64) {
	d.mu.Lock()
	defer d.mu.Unlock()
	vib = append([]float64(nil), d.vib...)
	suc = append([]float64(nil), d.suc...)
	return
}

func TestExtendedOScalesAmplitudeKeepsRhythm(t *testing.T) {
	dev := &recordingDev{}
	p := New(dev)

	// Einfache auf/ab-Kurve: 0 → 1 → 0 → 1 … damit sichtbar bleibt, dass
	// der Rhythmus während EO nicht einfriert.
	frames := make([]funscript.Frame, 0, 20)
	for i := 0; i < 20; i++ {
		v := 0.0
		if i%2 == 1 {
			v = 1.0
		}
		frames = append(frames, funscript.Frame{
			At:        int64(i * 40),
			Vibration: v,
			Suction:   v * 0.8,
		})
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	done := make(chan error, 1)
	go func() { done <- p.Play(ctx, frames) }()

	// Kurz anlaufen lassen, dann EO mit Faktor 0.5 und ohne Restore-Rampe.
	time.Sleep(30 * time.Millisecond)
	p.TriggerExtendedO(ExtendedOOptions{
		MinLevel:        0.5,
		HoldDuration:    200 * time.Millisecond,
		RestoreDuration: 0,
	})

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Play: %v", err)
		}
	case <-time.After(3 * time.Second):
		cancel()
		t.Fatal("Play timeout")
	}

	vib, _ := dev.snapshot()
	if len(vib) < 8 {
		t.Fatalf("zu wenige Ausgaben: %d", len(vib))
	}

	// Während EO bei Scale 0.5 müssen Peak-Frames (~1.0) als ~0.5 ankommen,
	// und es muss weiterhin Variation geben (nicht alles flach auf 0.5).
	sawHalfPeak := false
	sawZero := false
	distinct := map[int]bool{}
	for _, v := range vib {
		distinct[int(math.Round(v*100))] = true
		if math.Abs(v-0.5) < 0.05 {
			sawHalfPeak = true
		}
		if v < 0.05 {
			sawZero = true
		}
		// Nie flach auf einem "MinLevel als Absolutwert" ohne dass 0 vorkommt
		// bei ungeraden/geraden Frames - und nie volle 1.0 während Hold,
		// sofern Scale aktiv war. Volles 1.0 vor/nach EO ist ok.
	}
	if !sawHalfPeak {
		t.Fatalf("erwartete skalierte Peaks (~0.5), bekam %v", vib)
	}
	if !sawZero {
		t.Fatalf("erwartete weiterhin Nullen (Rhythmus), bekam %v", vib)
	}
	if len(distinct) < 2 {
		t.Fatalf("Kurve darf während EO nicht flach werden, distinct=%v vib=%v", distinct, vib)
	}
}

func TestSetOutputAppliesIntensityScale(t *testing.T) {
	dev := &recordingDev{}
	p := New(dev)
	p.setIntensityScale(0.25)
	if err := p.setOutput(funscript.Frame{At: 10, Vibration: 0.8, Suction: 0.4}); err != nil {
		t.Fatal(err)
	}
	vib, suc := dev.snapshot()
	if len(vib) != 1 || math.Abs(vib[0]-0.2) > 1e-9 {
		t.Fatalf("vib: want 0.2, got %v", vib)
	}
	if len(suc) != 1 || math.Abs(suc[0]-0.1) > 1e-9 {
		t.Fatalf("suc: want 0.1, got %v", suc)
	}
}

// MinLevel 0 must silence the device, not read back as full power: the old
// "scale <= 0 means 1" guard turned the end of the ramp and the whole hold
// into full intensity.
func TestExtendedOMinLevelZeroSilences(t *testing.T) {
	dev := &recordingDev{}
	p := New(dev)
	p.setIntensityScale(0)
	if got := p.getIntensityScale(); got != 0 {
		t.Fatalf("scale 0 read back as %v", got)
	}
	if err := p.setOutput(funscript.Frame{At: 10, Vibration: 0.8, Suction: 0.4}); err != nil {
		t.Fatal(err)
	}
	vib, suc := dev.snapshot()
	if vib[0] != 0 || suc[0] != 0 {
		t.Fatalf("MinLevel 0 must be silent, got vib=%v suc=%v", vib, suc)
	}
}

func TestZeroValuePlayerPlaysFullHeight(t *testing.T) {
	var p Player // not via New: must still play at full curve height
	if got := p.getIntensityScale(); got != 1 {
		t.Fatalf("zero-value scale = %v, want 1", got)
	}
}
