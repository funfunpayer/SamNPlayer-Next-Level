package main

import (
	"math"
	"math/rand"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// burstyScript: strokes with irregular busy and calm stretches over 120 s,
// so the activity has one clear alignment (not a pure period).
func burstyScript(seed int64) ([]funscript.Action, []bool) {
	rng := rand.New(rand.NewSource(seed))
	var acts []funscript.Action
	busy := make([]bool, 120)
	for s := range busy {
		busy[s] = rng.Float64() < 0.45
	}
	t, up := int64(0), false
	for t < 120_000 {
		pos := 10
		if up {
			pos = 90
		}
		acts = append(acts, funscript.Action{At: t, Pos: pos})
		up = !up
		if busy[t/1000] {
			t += 180 + rng.Int63n(60)
		} else {
			t += 900 + rng.Int63n(300)
		}
	}
	return acts, busy
}

// soundFor: loudness that follows the script activity, delayed by lagMs.
func soundFor(acts []funscript.Action, lagMs int64, noise float64, seed int64) []float64 {
	act := scriptActivity(acts, 50, 2400)
	rng := rand.New(rand.NewSource(seed))
	out := make([]float64, len(act))
	shift := int(lagMs / 50)
	for i := range out {
		k := i - shift
		v := 0.0
		if k >= 0 && k < len(act) {
			v = act[k]
		}
		out[i] = math.Min(1, v*1.5+noise*rng.Float64())
	}
	return out
}

func TestEstimateSyncLagFindsDelay(t *testing.T) {
	acts, _ := burstyScript(1)
	for _, lag := range []int64{-1200, -350, 0, 400, 2000} {
		e := estimateSyncLag(soundFor(acts, lag, 0.2, 2), 50, acts, autoSyncMaxLagMs)
		if !e.OK || e.LagMs != lag {
			t.Fatalf("lag %d: got %+v", lag, e)
		}
		if c := autoSyncConfidence(e); c != "high" {
			t.Fatalf("lag %d: confidence %q (%+v)", lag, c, e)
		}
	}
}

func TestEstimateSyncLagNoMatch(t *testing.T) {
	acts, _ := burstyScript(1)
	rng := rand.New(rand.NewSource(9))
	noise := make([]float64, 2400)
	for i := range noise {
		noise[i] = rng.Float64()
	}
	e := estimateSyncLag(noise, 50, acts, autoSyncMaxLagMs)
	if c := autoSyncConfidence(e); c == "high" {
		t.Fatalf("unrelated sound must not be a clear match: %+v", e)
	}
	flat := make([]float64, 2400)
	if e := estimateSyncLag(flat, 50, acts, autoSyncMaxLagMs); e.OK || autoSyncConfidence(e) != "none" {
		t.Fatalf("silent audio: %+v", e)
	}
	if e := estimateSyncLag(noise, 50, acts[:3], autoSyncMaxLagMs); e.OK {
		t.Fatalf("too few actions must not estimate: %+v", e)
	}
}

func TestEstimateSyncLagPeriodicIsNotHigh(t *testing.T) {
	// A perfectly even rhythm matches at every period: never "high".
	var acts []funscript.Action
	for i := int64(0); i < 300; i++ {
		acts = append(acts, funscript.Action{At: i * 400, Pos: int(10 + 80*(i%2))})
	}
	audio := make([]float64, 2400)
	for i := range audio {
		audio[i] = 0.5 + 0.5*math.Sin(float64(i)*50*2*math.Pi/800)
	}
	if c := autoSyncConfidence(estimateSyncLag(audio, 50, acts, autoSyncMaxLagMs)); c == "high" {
		t.Fatal("periodic signal must not be a clear match")
	}
}

func TestEstimateAudioSyncOffsetNeedsScriptAndVideo(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if _, err := a.EstimateAudioSyncOffset(); err == nil {
		t.Fatal("no script: want error")
	}
}
