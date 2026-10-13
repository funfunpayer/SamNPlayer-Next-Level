package posttrack

import (
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

type goldenCase struct {
	Name         string         `json:"name"`
	Kind         string         `json:"kind"`
	Kwargs       map[string]any `json:"kwargs"`
	TimestampsMs []float64      `json:"timestamps_ms"`
	Positions    []float64      `json:"positions"`
	Actions      []goldenAction `json:"actions"`
	DensePos     []float64      `json:"dense_pos"`
	MaxSpeed     float64        `json:"max_speed"`
	ActionsIn    []goldenAction `json:"actions_in"`
	ActionsOut   []goldenAction `json:"actions_out"`
	Changed      int            `json:"changed"`
}

type goldenAction struct {
	At  int64 `json:"at"`
	Pos int   `json:"pos"`
}

func loadGoldens(t *testing.T) []goldenCase {
	t.Helper()
	path := filepath.Join("testdata", "positions_goldens.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read goldens: %v", err)
	}
	var cases []goldenCase
	if err := json.Unmarshal(data, &cases); err != nil {
		t.Fatalf("parse goldens: %v", err)
	}
	return cases
}

func optsFromKwargs(kwargs map[string]any) Options {
	opts := DefaultOptions()
	if kwargs == nil {
		return opts
	}
	if v, ok := kwargs["invert"].(bool); ok {
		opts.Invert = v
	}
	if v, ok := kwargs["smooth_window"].(float64); ok {
		opts.SmoothWindow = int(v)
	}
	if v, ok := kwargs["min_peak_distance_ms"].(float64); ok {
		opts.MinPeakDistanceMs = int(v)
	}
	if v, ok := kwargs["rdp_tolerance"].(float64); ok {
		opts.RDPTolerance = v
	}
	if v, ok := kwargs["norm_percentile"].(float64); ok {
		opts.NormPercentile = v
	}
	if v, ok := kwargs["adaptive_error"].(float64); ok {
		opts.AdaptiveError = v
	}
	if v, ok := kwargs["min_interval_ms"].(float64); ok {
		opts.MinIntervalMs = v
	}
	if v, ok := kwargs["dynamic_range_ms"].(float64); ok {
		opts.DynamicRangeMs = v
	}
	if v, ok := kwargs["peak_prominence"].(float64); ok {
		opts.PeakProminence = v
	}
	return opts
}

func TestPositionsToActionsMatchesPythonGoldens(t *testing.T) {
	for _, gc := range loadGoldens(t) {
		gc := gc
		if gc.Kind == "limit_speed" {
			continue
		}
		t.Run(gc.Name, func(t *testing.T) {
			opts := optsFromKwargs(gc.Kwargs)
			got, err := PositionsToActions(gc.TimestampsMs, gc.Positions, opts)
			if err != nil {
				t.Fatalf("PositionsToActions: %v", err)
			}
			if len(got.Actions) != len(gc.Actions) {
				t.Fatalf("action count: got %d want %d", len(got.Actions), len(gc.Actions))
			}
			for i := range gc.Actions {
				if got.Actions[i].At != gc.Actions[i].At || got.Actions[i].Pos != gc.Actions[i].Pos {
					t.Fatalf("action[%d]: got {%d,%d} want {%d,%d}",
						i, got.Actions[i].At, got.Actions[i].Pos,
						gc.Actions[i].At, gc.Actions[i].Pos)
				}
			}
			if len(got.DensePos) != len(gc.DensePos) {
				t.Fatalf("dense length: got %d want %d", len(got.DensePos), len(gc.DensePos))
			}
			maxDiff := 0.0
			for i := range gc.DensePos {
				d := math.Abs(got.DensePos[i] - gc.DensePos[i])
				if d > maxDiff {
					maxDiff = d
				}
			}
			// Floating-point path (savgol / percentiles) should stay well
			// under half a funscript position unit against scipy/numpy.
			if maxDiff > 0.05 {
				t.Fatalf("dense_pos max abs diff %.6f > 0.05", maxDiff)
			}
		})
	}
}

func TestLimitSpeedMatchesPythonGolden(t *testing.T) {
	for _, gc := range loadGoldens(t) {
		if gc.Kind != "limit_speed" {
			continue
		}
		in := make([]funscript.Action, len(gc.ActionsIn))
		for i, a := range gc.ActionsIn {
			in[i] = funscript.Action{At: a.At, Pos: a.Pos}
		}
		out, changed := LimitSpeed(in, gc.MaxSpeed)
		if changed != gc.Changed {
			t.Fatalf("changed: got %d want %d", changed, gc.Changed)
		}
		if len(out) != len(gc.ActionsOut) {
			t.Fatalf("len: got %d want %d", len(out), len(gc.ActionsOut))
		}
		for i := range out {
			if out[i].At != gc.ActionsOut[i].At || out[i].Pos != gc.ActionsOut[i].Pos {
				t.Fatalf("out[%d]: got {%d,%d} want {%d,%d}",
					i, out[i].At, out[i].Pos, gc.ActionsOut[i].At, gc.ActionsOut[i].Pos)
			}
		}
	}
}

func TestProminenceReducesDampedRinging(t *testing.T) {
	// Behavioural mirror of quality_doctor_test.py's weich-profile check:
	// damped ringing produces many keyframes without prominence and far
	// fewer with it; a clean stroke is unchanged.
	var without, with goldenCase
	for _, gc := range loadGoldens(t) {
		switch gc.Name {
		case "damped_no_prom":
			without = gc
		case "damped_prom035":
			with = gc
		}
	}
	if without.Name == "" || with.Name == "" {
		t.Fatal("missing damped goldens")
	}
	if !(len(without.Actions) > 70) {
		t.Fatalf("expected many keyframes without prominence, got %d", len(without.Actions))
	}
	if !(len(with.Actions) < len(without.Actions)) {
		t.Fatalf("prominence should reduce keyframes: %d -> %d", len(without.Actions), len(with.Actions))
	}
}

func TestPercentileBeatsMinMaxOnOutlier(t *testing.T) {
	var minmax, perc goldenCase
	for _, gc := range loadGoldens(t) {
		switch gc.Name {
		case "outlier_minmax":
			minmax = gc
		case "outlier_percentile":
			perc = gc
		}
	}
	// With min/max the spike owns the scale; percentile restores range.
	// Compare dense signal span used by the real motion (exclude spike frames).
	span := func(dense []float64, skipLo, skipHi int) float64 {
		lo, hi := 100.0, 0.0
		for i, v := range dense {
			if i >= skipLo && i < skipHi {
				continue
			}
			if v < lo {
				lo = v
			}
			if v > hi {
				hi = v
			}
		}
		return hi - lo
	}
	sMin := span(minmax.DensePos, 20, 26)
	sPerc := span(perc.DensePos, 20, 26)
	if sPerc <= sMin {
		t.Fatalf("percentile should restore motion span: perc=%.1f minmax=%.1f", sPerc, sMin)
	}
}
