package generator

import "testing"

func TestTrackingGapsFromFlags(t *testing.T) {
	if gaps := trackingGapsFromFlags(nil, nil, 100); gaps != nil {
		t.Fatalf("empty → nil, got %v", gaps)
	}
	ts := []int{0, 40, 80}
	if gaps := trackingGapsFromFlags(ts, []bool{false, false, false}, 100); gaps != nil {
		t.Fatalf("no lost → nil, got %v", gaps)
	}
	ts = []int{0, 40, 80, 120, 160}
	lost := []bool{false, true, true, false, true}
	gaps := trackingGapsFromFlags(ts, lost, 100)
	// Clear stretch 80→160 is only 80ms (< merge 100) → one merged gap.
	if len(gaps) != 1 {
		t.Fatalf("want 1 merged gap, got %v", gaps)
	}
	if gaps[0].StartMs != 40 || gaps[0].EndMs != 160 {
		t.Fatalf("gap0 = %+v", gaps[0])
	}
	// Separate with mergeGap smaller than the clear stretch.
	gaps = trackingGapsFromFlags(ts, lost, 50)
	if len(gaps) != 2 {
		t.Fatalf("want 2 gaps with merge=50, got %v", gaps)
	}
	if gaps[0].StartMs != 40 || gaps[0].EndMs != 80 {
		t.Fatalf("gap0 = %+v", gaps[0])
	}
	if gaps[1].StartMs != 160 || gaps[1].EndMs != 160 {
		t.Fatalf("gap1 = %+v", gaps[1])
	}
	// Merge across short clear stretch (< 100ms).
	ts = []int{0, 40, 80, 100, 140}
	lost = []bool{true, true, false, true, true}
	gaps = trackingGapsFromFlags(ts, lost, 100)
	if len(gaps) != 1 {
		t.Fatalf("expected merge, got %v", gaps)
	}
	if gaps[0].StartMs != 0 || gaps[0].EndMs != 140 {
		t.Fatalf("merged = %+v", gaps[0])
	}
}
