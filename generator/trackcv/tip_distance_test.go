//go:build cgo && opencv

package trackcv

import (
	"math"
	"testing"
)

func TestTipPartnerDistance_smallTipMatchesCenter(t *testing.T) {
	tip := Rect{X: 100, Y: 100, W: 4, H: 4}
	partner := Rect{X: 200, Y: 100, W: 10, H: 10}
	d := TipPartnerDistance(tip, partner)
	// Closest tip point to partner center (205,105) is (104,104).
	want := math.Hypot(205-104, 105-104)
	if math.Abs(d-want) > 0.01 {
		t.Fatalf("small tip: got %v want ~%v", d, want)
	}
}

func TestTipPartnerDistance_largePenisUsesNearEdge(t *testing.T) {
	// Whole-penis tip: partner to the right → distance from right edge, not center.
	tip := Rect{X: 10, Y: 40, W: 100, H: 20}     // right edge x=110
	partner := Rect{X: 120, Y: 45, W: 20, H: 10} // center (130, 50)
	d := TipPartnerDistance(tip, partner)
	if math.Abs(d-20) > 0.01 {
		t.Fatalf("large tip edge: got %v want 20", d)
	}
	centerDist := math.Hypot(130-60, 50-50)
	if d >= centerDist {
		t.Fatalf("edge distance %v should be < center %v", d, centerDist)
	}
}

func TestTipPartnerDistance_overlapIsZero(t *testing.T) {
	tip := Rect{X: 0, Y: 0, W: 50, H: 50}
	partner := Rect{X: 10, Y: 10, W: 10, H: 10}
	if d := TipPartnerDistance(tip, partner); d != 0 {
		t.Fatalf("overlap: got %v want 0", d)
	}
}

func TestFuseTipPartners_excludesLostPartner(t *testing.T) {
	tip := Rect{X: 0, Y: 0, W: 10, H: 10}
	near := Rect{X: 20, Y: 0, W: 10, H: 10} // dist ~10 from tip edge
	far := Rect{X: 200, Y: 0, W: 10, H: 10} // much farther
	// If both included, min is near.
	d, ok := FuseTipPartners(tip, true, []Rect{near, far}, []bool{true, true})
	if !ok || d > 15 {
		t.Fatalf("both: got %v ok=%v", d, ok)
	}
	// Stale "near" lost → only far counts (must NOT keep dragging min down).
	d, ok = FuseTipPartners(tip, true, []Rect{near, far}, []bool{false, true})
	if !ok {
		t.Fatal("expected ok with remaining partner")
	}
	if d < 100 {
		t.Fatalf("lost near must leave far distance, got %v", d)
	}
	// Tip lost → no fresh distance.
	if _, ok = FuseTipPartners(tip, false, []Rect{near}, []bool{true}); ok {
		t.Fatal("tip lost must not fuse")
	}
	// All partners lost → no fresh distance.
	if _, ok = FuseTipPartners(tip, true, []Rect{near, far}, []bool{false, false}); ok {
		t.Fatal("no partners must not fuse")
	}
}
