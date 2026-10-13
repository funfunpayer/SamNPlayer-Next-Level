package trackutil

import "testing"

func TestCoastBridgesBriefLoss(t *testing.T) {
	var c Coast
	c.ObserveOK(10, 20, 40, 40)
	c.ObserveOK(12, 22, 40, 40) // vx=2, vy=2
	x, y, w, h, ok := c.OnLost()
	if !ok {
		t.Fatal("expected include during coast")
	}
	if w != 40 || h != 40 {
		t.Fatalf("size %dx%d", w, h)
	}
	if x != 14 || y != 24 {
		t.Fatalf("coasted to %d,%d want 14,24", x, y)
	}
}

func TestCoastExhausts(t *testing.T) {
	c := Coast{MaxFrames: 2}
	c.ObserveOK(0, 0, 10, 10)
	c.ObserveOK(1, 0, 10, 10)
	if _, _, _, _, ok := c.OnLost(); !ok {
		t.Fatal("frame 1 should coast")
	}
	if _, _, _, _, ok := c.OnLost(); !ok {
		t.Fatal("frame 2 should coast")
	}
	if _, _, _, _, ok := c.OnLost(); ok {
		t.Fatal("frame 3 should exclude")
	}
}

func TestCoastHoldsWithoutVelocity(t *testing.T) {
	var c Coast
	c.ObserveOK(5, 5, 10, 10) // one sample — hold box, do not invent motion
	x, y, _, _, ok := c.OnLost()
	if !ok {
		t.Fatal("should hold last box")
	}
	if x != 5 || y != 5 {
		t.Fatalf("held %d,%d want 5,5", x, y)
	}
}

func TestCoastNoPosition(t *testing.T) {
	var c Coast
	if _, _, _, _, ok := c.OnLost(); ok {
		t.Fatal("empty coast must not include")
	}
}

// After appearance reacquire re-anchors, callers Reset() then ObserveOK —
// lost streak and velocity must clear so the new tip does not inherit the
// pre-loss coast budget / trajectory (hard-tip golden: wrong coast after snap).
func TestCoastResetAfterReacquire(t *testing.T) {
	var c Coast
	c.ObserveOK(10, 20, 30, 30)
	c.ObserveOK(12, 22, 30, 30)
	if _, _, _, _, ok := c.OnLost(); !ok {
		t.Fatal("pre-reset coast should include")
	}
	if c.LostStreak() != 1 {
		t.Fatalf("lost streak %d want 1", c.LostStreak())
	}
	c.Reset()
	if c.LostStreak() != 0 {
		t.Fatalf("after Reset lost streak %d want 0", c.LostStreak())
	}
	// Re-anchored tip at a new place — hold, do not invent old velocity.
	c.ObserveOK(100, 100, 30, 30)
	x, y, _, _, ok := c.OnLost()
	if !ok {
		t.Fatal("should coast-hold after fresh ObserveOK")
	}
	if x != 100 || y != 100 {
		t.Fatalf("held %d,%d want 100,100 (no inherited velocity)", x, y)
	}
}

// Exhausting coast then ObserveOK must reopen the budget (brief tip loss
// recovery after a longer occlusion window ended).
func TestCoastBudgetReopensAfterObserveOK(t *testing.T) {
	c := Coast{MaxFrames: 2}
	c.ObserveOK(0, 0, 10, 10)
	c.ObserveOK(1, 0, 10, 10)
	c.OnLost()
	c.OnLost()
	if _, _, _, _, ok := c.OnLost(); ok {
		t.Fatal("budget should be exhausted")
	}
	c.ObserveOK(5, 5, 10, 10)
	if _, _, _, _, ok := c.OnLost(); !ok {
		t.Fatal("ObserveOK must reopen coast budget")
	}
}
