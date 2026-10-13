package player

import (
	"context"
	"math"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestClampSpeed(t *testing.T) {
	for in, want := range map[float64]float64{0: 1, -1: 1, 0.2: MinSpeed, 1.5: 1.5, 9: MaxSpeed} {
		if got := ClampSpeed(in); got != want {
			t.Fatalf("ClampSpeed(%v) = %v, want %v", in, got, want)
		}
	}
}

func TestSpeedScalesClockAndReanchors(t *testing.T) {
	p := New(nil)
	t0 := time.Now()
	p.startClock(t0)
	p.SetSpeed(2)
	// After re-anchoring at ~t0, 1000 ms of script is due ~500 ms later.
	d := p.realTimeFor(1000).Sub(t0)
	if d < 480*time.Millisecond || d > 540*time.Millisecond {
		t.Fatalf("2× speed: 1000 ms script due after %v, want ~500ms", d)
	}
	p.SetSpeed(0.5)
	d = p.realTimeFor(1000).Sub(t0)
	if d < 1950*time.Millisecond || d > 2100*time.Millisecond {
		t.Fatalf("0.5× speed: 1000 ms script due after %v, want ~2000ms", d)
	}
	if p.Speed() != 0.5 {
		t.Fatalf("Speed() = %v", p.Speed())
	}
}

func TestChannelLevelsDefaultFullAndClamp(t *testing.T) {
	var p Player // zero value, as in tests without New()
	if v, s := p.ChannelLevels(); v != 1 || s != 1 {
		t.Fatalf("default levels %v %v, want full", v, s)
	}
	p.SetChannelLevels(0.4, -3)
	if v, s := p.ChannelLevels(); v != 0.4 || s != 0 {
		t.Fatalf("levels %v %v, want 0.4 0", v, s)
	}
	p.SetChannelLevels(7, 0.25)
	if v, s := p.ChannelLevels(); v != 1 || s != 0.25 {
		t.Fatalf("levels %v %v, want 1 0.25", v, s)
	}
}

func TestReducePeak(t *testing.T) {
	cases := map[float64]float64{0: 0, 0.3: 0.3, PeakKnee: PeakKnee, 0.8: 0.68, 1: 0.76}
	for in, want := range cases {
		if got := ReducePeak(in); math.Abs(got-want) > 1e-9 {
			t.Errorf("ReducePeak(%v) = %v, want %v", in, got, want)
		}
	}
	// Monotonic: a stronger input never comes out weaker.
	prev := -1.0
	for v := 0.0; v <= 1.0001; v += 0.01 {
		if got := ReducePeak(v); got < prev {
			t.Fatalf("not monotonic at %v", v)
		} else {
			prev = got
		}
	}
}

func TestSetOutputReducePeaks(t *testing.T) {
	dev := &recordingDev{}
	p := New(dev)
	if p.PeakReduce() {
		t.Fatal("Reduce peaks must be off by default")
	}
	p.SetPeakReduce(true)
	p.SetChannelLevels(0.5, 1)
	if err := p.setOutput(funscript.Frame{At: 10, Vibration: 1, Suction: 0.4}); err != nil {
		t.Fatal(err)
	}
	// Peak first, then the channel level: 1 → 0.76 → ×0.5; 0.4 stays below the knee.
	vib, suc := dev.snapshot()
	if got := vib[len(vib)-1]; math.Abs(got-0.38) > 1e-9 {
		t.Fatalf("vibration %v, want 0.38", got)
	}
	if got := suc[len(suc)-1]; math.Abs(got-0.4) > 1e-9 {
		t.Fatalf("suction %v, want 0.4", got)
	}
	p.SetPeakReduce(false)
	if err := p.setOutput(funscript.Frame{At: 20, Vibration: 1, Suction: 1}); err != nil {
		t.Fatal(err)
	}
	vib, _ = dev.snapshot()
	if got := vib[len(vib)-1]; math.Abs(got-0.5) > 1e-9 {
		t.Fatalf("off: vibration %v, want 0.5", got)
	}
}

func TestPlayStartAtMsResumes(t *testing.T) {
	dev := &recordingDev{}
	p := New(dev)
	p.StartAtMs = 1000
	frames := []funscript.Frame{
		{At: 0, Vibration: 0.1}, {At: 500, Vibration: 0.2},
		{At: 1000, Vibration: 0.3}, {At: 1040, Vibration: 0.4},
	}
	start := time.Now()
	if err := p.Play(context.Background(), frames); err != nil {
		t.Fatal(err)
	}
	if el := time.Since(start); el > 400*time.Millisecond {
		t.Fatalf("resume at 1000 ms must not wait for the skipped second, took %v", el)
	}
	vib, _ := dev.snapshot()
	if len(vib) != 2 || vib[0] != 0.3 || vib[1] != 0.4 {
		t.Fatalf("resumed output %v, want [0.3 0.4]", vib)
	}
	if got := framesFrom(frames, 5000); len(got) != 1 || got[0].At != 1040 {
		t.Fatalf("past the end keeps the last frame, got %v", got)
	}
	if got := framesFrom(frames, 0); len(got) != 4 {
		t.Fatal("0 keeps all frames")
	}
}
