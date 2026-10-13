package generator

import "testing"

func TestAdaptiveDetrendTwoStrokePeriods(t *testing.T) {
	cases := []struct {
		hint map[string]any
		want float64
	}{
		// clip_voll's measured tempo: 1 Hz -> 2000ms, the best-scoring window.
		{map[string]any{"quality": "ok", "stroke_hz": 1.0}, 2000},
		// clip_ausschnitt's 1.5 Hz -> 1333 clamps up to the 1500 floor.
		{map[string]any{"quality": "ok", "stroke_hz": 1.5}, 1500},
		// slow stroke: 2x period capped so the window can't swallow it.
		{map[string]any{"quality": "ok", "stroke_hz": 0.4}, 4000},
		// unreliable tempo -> conservative default.
		{map[string]any{"quality": "weak", "stroke_hz": 1.0}, 3000},
		{map[string]any{"quality": "ok", "stroke_hz": 5.0}, 3000},
		{map[string]any{"quality": "ok"}, 3000},
		{nil, 3000},
	}
	for _, c := range cases {
		if got, _ := adaptiveDetrendMs(c.hint); got != c.want {
			t.Errorf("hint=%v: got %.0f, want %.0f", c.hint, got, c.want)
		}
	}
}

func TestApplyDefaultDetrendRespectsCallerAndDistanceProfiles(t *testing.T) {
	hint := map[string]any{"quality": "ok", "stroke_hz": 1.0}

	if got := applyDefaultDetrend(Options{StrokePreviewHint: hint}, nil).DetrendWindowMs; got != 2000 {
		t.Errorf("stroke profile unset: got %v, want 2000", got)
	}
	if got := applyDefaultDetrend(Options{Profile: "autotune", StrokePreviewHint: hint}, nil).DetrendWindowMs; got != 2000 {
		t.Errorf("autotune unset: got %v, want 2000", got)
	}
	if got := applyDefaultDetrend(Options{DetrendWindowMs: 5000, StrokePreviewHint: hint}, nil).DetrendWindowMs; got != 5000 {
		t.Errorf("caller value overridden: got %v", got)
	}
	if got := applyDefaultDetrend(Options{DetrendWindowMs: -1, StrokePreviewHint: hint}, nil).DetrendWindowMs; got != -1 {
		t.Errorf("explicit off overridden: got %v", got)
	}
	// Distance profiles: the absolute value is the signal (contact = ~0).
	for _, p := range []string{"tj", "tf"} {
		if got := applyDefaultDetrend(Options{Profile: p, StrokePreviewHint: hint}, nil).DetrendWindowMs; got != 0 {
			t.Errorf("profile %s must stay undetrended, got %v", p, got)
		}
	}
}
