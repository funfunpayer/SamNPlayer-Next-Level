package funscript

import "testing"

func TestSuggestPolarityLowFirstHalf(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 10}, {At: 100, Pos: 20}, {At: 200, Pos: 15},
		{At: 300, Pos: 80}, {At: 400, Pos: 90},
	}
	h := SuggestPolarity(actions)
	if !h.SuggestInvert {
		t.Fatalf("erwartete Invert-Empfehlung, got %+v", h)
	}
}

func TestSuggestPolarityHighFirstHalf(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 80}, {At: 100, Pos: 90}, {At: 200, Pos: 85},
		{At: 300, Pos: 20}, {At: 400, Pos: 10},
	}
	h := SuggestPolarity(actions)
	if h.SuggestInvert {
		t.Fatalf("keine Invert-Empfehlung erwartet, got %+v", h)
	}
}

func TestSuggestPolarityTooFewPoints(t *testing.T) {
	h := SuggestPolarity([]Action{{At: 0, Pos: 0}, {At: 1, Pos: 1}})
	if h.SuggestInvert || h.Reason == "" {
		t.Fatalf("zu wenige Punkte müssen ohne Invert enden: %+v", h)
	}
}
