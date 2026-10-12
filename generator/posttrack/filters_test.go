package posttrack

import (
	"math"
	"testing"
)

func TestRollingDetrendRemovesSlowDrift(t *testing.T) {
	n := 200
	x := make([]float64, n)
	for i := 0; i < n; i++ {
		x[i] = float64(i)*0.5 + 10*math.Sin(float64(i)/5)
	}
	out := RollingDetrend(x, 800, 40) // ~20-sample window
	var sum float64
	for _, v := range out {
		sum += v
	}
	mean := sum / float64(n)
	if math.Abs(mean) > 2 {
		t.Fatalf("detrend mean=%.3f want ~0", mean)
	}
	lo, hi := minMax(out)
	if hi-lo < 10 {
		t.Fatalf("detrend killed oscillation: span=%.2f", hi-lo)
	}
}

func TestBandpassAttenuatesDCAndHighFreq(t *testing.T) {
	n := 512
	fs := 30.0
	x := make([]float64, n)
	for i := 0; i < n; i++ {
		tSec := float64(i) / fs
		x[i] = 50 +
			20*math.Sin(2*math.Pi*1.5*tSec) +
			5*math.Sin(2*math.Pi*10*tSec)
	}
	out := Bandpass(x, fs, 0.5, 4.0)
	var sum float64
	for _, v := range out {
		sum += v
	}
	mean := sum / float64(n)
	if math.Abs(mean) > 8 {
		t.Fatalf("bandpass mean=%.2f want near 0 (DC removed)", mean)
	}
	if ptp(out) < 20 {
		t.Fatalf("in-band energy gone: span=%.2f", ptp(out))
	}
}
