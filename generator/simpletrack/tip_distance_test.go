package simpletrack

import (
	"math"
	"testing"
)

func TestTipPartnerDistance_largePenisUsesNearEdge(t *testing.T) {
	tip := Rect{X: 10, Y: 40, W: 100, H: 20}
	partner := Rect{X: 120, Y: 45, W: 20, H: 10}
	d := tipPartnerDistance(tip, partner)
	if math.Abs(d-20) > 0.01 {
		t.Fatalf("got %v want 20", d)
	}
}

func TestTipPartnerDistance_overlapIsZero(t *testing.T) {
	if d := tipPartnerDistance(Rect{0, 0, 50, 50}, Rect{10, 10, 10, 10}); d != 0 {
		t.Fatalf("got %v want 0", d)
	}
}

func TestFuseTipPartners_excludesLost(t *testing.T) {
	tip := Rect{X: 0, Y: 0, W: 10, H: 10}
	near := Rect{X: 20, Y: 0, W: 10, H: 10}
	far := Rect{X: 200, Y: 0, W: 10, H: 10}
	d, ok := fuseTipPartners(tip, true, []Rect{near, far}, []bool{false, true})
	if !ok || d < 100 {
		t.Fatalf("lost near must leave far, got %v ok=%v", d, ok)
	}
}
