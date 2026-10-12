package funscript

import (
	"math"
	"reflect"
	"testing"
)

// strokes: turning points every halfMs alternating lo/hi over [0, durMs].
func strokes(durMs, halfMs int64, lo, hi int) []Action {
	var out []Action
	for t, up := int64(0), false; t <= durMs; t, up = t+halfMs, !up {
		p := lo
		if up {
			p = hi
		}
		out = append(out, Action{At: t, Pos: p})
	}
	return out
}

// dense: a 50 ms sampled sine with the given half-period.
func dense(durMs, halfMs int64) []Action {
	var out []Action
	for t := int64(0); t <= durMs; t += 50 {
		out = append(out, Action{At: t, Pos: int(math.Round(50 - 40*math.Cos(math.Pi*float64(t)/float64(halfMs))))})
	}
	return out
}

func inside(a []Action, s, e int64) []Action {
	var out []Action
	for _, x := range a {
		if x.At > s && x.At < e {
			out = append(out, x)
		}
	}
	return out
}

func TestRhythmBridgeRestoresRegularStrokes(t *testing.T) {
	orig := strokes(30000, 400, 10, 90)
	out, n, pts := RhythmBridgeSpans(orig, []TrackingGap{{StartMs: 10100, EndMs: 14100}}, 0)
	want := len(inside(orig, 10000, 14400))
	if n != 1 || pts != want {
		t.Fatalf("filled %d spans, %d points; want 1 and %d", n, pts, want)
	}
	for _, a := range inside(out, 10000, 14400) {
		k := float64(a.At) / 400
		if math.Abs(k-math.Round(k)) > 0.05 {
			t.Fatalf("point at %d ms off the 400 ms grid", a.At)
		}
		level := 10
		if int64(math.Round(k))%2 == 1 {
			level = 90
		}
		if a.Pos != level {
			t.Fatalf("point at %d ms: pos %d, want %d (alternation lost)", a.At, a.Pos, level)
		}
	}
}

func TestRhythmBridgeReadsDenseScripts(t *testing.T) {
	orig := dense(30000, 500)
	out, n, pts := RhythmBridgeSpans(orig, []TrackingGap{{StartMs: 12000, EndMs: 16000}}, 0)
	if n != 1 {
		t.Fatal("dense span not filled")
	}
	// ~4 s / 500 ms = 8 half-periods -> ~7 turning points inside, not 80 samples.
	if pts < 6 || pts > 9 {
		t.Fatalf("dense: %d points inside, want ~7 turning points", pts)
	}
	for _, a := range inside(out, 12000, 16000) {
		if a.Pos > 20 && a.Pos < 80 {
			t.Fatalf("bridge point %d at %d ms is not at a turning-point level", a.Pos, a.At)
		}
	}
}

func TestRhythmBridgeLeavesSpanWithoutRhythm(t *testing.T) {
	flat := []Action{{At: 0, Pos: 50}, {At: 5000, Pos: 50}, {At: 10000, Pos: 51}, {At: 20000, Pos: 50}, {At: 25000, Pos: 50}}
	out, n, pts := RhythmBridgeSpans(flat, []TrackingGap{{StartMs: 9000, EndMs: 21000}}, 0)
	if n != 0 || pts != 0 || !reflect.DeepEqual(out, flat) {
		t.Fatalf("flat context must stay untouched: n=%d pts=%d out=%v", n, pts, out)
	}
}

func TestImproveHealRhythmIsOptIn(t *testing.T) {
	orig := strokes(30000, 400, 10, 90)
	gaps := []TrackingGap{{StartMs: 10100, EndMs: 14100}}
	// Off: exactly today's straight-line heal.
	res, err := ImproveScript(orig, ImproveOpts{HealTrackingGaps: true, TrackingGaps: gaps})
	if err != nil {
		t.Fatal(err)
	}
	line, _, _ := HealTrackingGaps(orig, gaps, 0, 0)
	if !reflect.DeepEqual(res.Actions, line) || res.RhythmBridged != 0 {
		t.Fatalf("HealRhythm off changed the heal (rhythm=%d)", res.RhythmBridged)
	}
	// On: rhythm bridge, gaps still cleared from metadata.
	res, err = ImproveScript(orig, ImproveOpts{HealTrackingGaps: true, TrackingGaps: gaps, HealRhythm: true})
	if err != nil {
		t.Fatal(err)
	}
	if res.WindowsHealed != 1 || res.RhythmBridged != 1 || !res.ClearTrackingGaps {
		t.Fatalf("on: %+v", res)
	}
}

func TestImproveRepairSpansFallsBackToLine(t *testing.T) {
	orig := strokes(30000, 400, 10, 90)
	// One span inside the rhythm, one inside a flat tail.
	withTail := append(append([]Action(nil), orig...),
		Action{At: 40000, Pos: 50}, Action{At: 60000, Pos: 50}, Action{At: 80000, Pos: 50})
	res, err := ImproveScript(withTail, ImproveOpts{RepairSpans: []TrackingGap{
		{StartMs: 5100, EndMs: 7100}, {StartMs: 45000, EndMs: 75000}}})
	if err != nil {
		t.Fatal(err)
	}
	if res.SpansRepaired != 2 || res.RhythmBridged != 1 {
		t.Fatalf("repaired %d, rhythm %d; want 2 and 1", res.SpansRepaired, res.RhythmBridged)
	}
	if res.ClearTrackingGaps {
		t.Fatal("user spans must not clear tracking_gaps metadata")
	}
}
