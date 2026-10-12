package funscript

import (
	"os"
	"testing"
)

func TestTrimActionsMiddle(t *testing.T) {
	in := []Action{{At: 0, Pos: 0}, {At: 1000, Pos: 100}, {At: 2000, Pos: 0}, {At: 3000, Pos: 50}}
	out, err := TrimActions(in, 500, 2500)
	if err != nil {
		t.Fatal(err)
	}
	if out[0].At != 500 || out[len(out)-1].At != 2500 {
		t.Fatalf("ends: %+v", out)
	}
	if out[0].Pos != 50 { // halfway 0→100
		t.Fatalf("start pos want 50 got %d", out[0].Pos)
	}
}

func TestFillGapsInsertsPoints(t *testing.T) {
	in := []Action{{At: 0, Pos: 0}, {At: 2000, Pos: 100}}
	out, gaps, added := FillGaps(in, 500, 500, 0)
	if gaps != 1 || added < 1 {
		t.Fatalf("gaps=%d added=%d out=%+v", gaps, added, out)
	}
	if out[0].At != 0 || out[len(out)-1].At != 2000 {
		t.Fatalf("endpoints changed: %+v", out)
	}
	// Midpoint at 1000 should be ~50
	found := false
	for _, a := range out {
		if a.At == 1000 {
			found = true
			if a.Pos != 50 {
				t.Fatalf("mid pos=%d", a.Pos)
			}
		}
	}
	if !found {
		t.Fatalf("missing midpoint: %+v", out)
	}
}

func TestFillGapsAudioHzStep(t *testing.T) {
	in := []Action{{At: 0, Pos: 0}, {At: 2000, Pos: 100}}
	// 1 Hz → half-period 500ms
	_, _, addedDefault := FillGaps(in, 500, 0, 0)
	_, _, addedAudio := FillGaps(in, 500, 0, 1.0)
	if addedAudio <= 0 || addedDefault <= 0 {
		t.Fatalf("default=%d audio=%d", addedDefault, addedAudio)
	}
}

func TestImproveScriptAutoSecondPass(t *testing.T) {
	// Fast stroke (~100ms) + long gap + medium hole (500ms) — second pass
	// at max(400, 4×median) fills the medium hole without needing MaxGapMs.
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 20},
		{At: 3000, Pos: 80}, // >800 → first pass
		{At: 3100, Pos: 90},
		{At: 3600, Pos: 40}, // 500ms hole → second pass only
		{At: 3700, Pos: 50},
	}
	res, err := ImproveScript(in, ImproveOpts{FillGaps: true}) // MaxGapMs 0 = auto+second
	if err != nil {
		t.Fatal(err)
	}
	if res.GapsFilled < 2 || res.PointsAdded < 2 {
		t.Fatalf("want ≥2 gaps filled, got gaps=%d pts=%d fillGapMs=%d",
			res.GapsFilled, res.PointsAdded, res.FillGapMs)
	}
}

func TestImproveScriptNoDensifySlowStroke(t *testing.T) {
	// Natural ~500ms extrema — fixed 400ms second pass would densify everything.
	in := make([]Action, 0, 20)
	for i := 0; i < 20; i++ {
		pos := 20
		if i%2 == 1 {
			pos = 85
		}
		in = append(in, Action{At: int64(i * 500), Pos: pos})
	}
	res, err := ImproveScript(in, ImproveOpts{FillGaps: true})
	if err != nil {
		t.Fatal(err)
	}
	if res.PointsAdded != 0 || res.GapsFilled != 0 {
		t.Fatalf("slow stroke densified: gaps=%d pts=%d after=%d",
			res.GapsFilled, res.PointsAdded, res.AfterCount)
	}
	if res.AfterCount != len(in) {
		t.Fatalf("count changed %d → %d", len(in), res.AfterCount)
	}
}

func TestImproveScriptTrimAndFill(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 10},
		{At: 5000, Pos: 90}, // big gap
		{At: 5100, Pos: 100},
	}
	res, err := ImproveScript(in, ImproveOpts{
		StartMs:  50,
		EndMs:    5050,
		FillGaps: true,
		MaxGapMs: 800,
		StepMs:   200,
	})
	if err != nil {
		t.Fatal(err)
	}
	if !res.Trimmed {
		t.Fatal("expected trim")
	}
	if res.GapsFilled < 1 || res.PointsAdded < 1 {
		t.Fatalf("fill: %+v", res)
	}
	if res.AfterCount <= res.BeforeCount {
		t.Fatalf("counts before=%d after=%d", res.BeforeCount, res.AfterCount)
	}
}

func TestHealTrackingGapsStripsInteriorAndBridges(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 10},
		{At: 1000, Pos: 99}, // junk inside loss
		{At: 1500, Pos: 98}, // junk
		{At: 2000, Pos: 90},
		{At: 2100, Pos: 100},
	}
	gaps := []TrackingGap{{StartMs: 500, EndMs: 1800}}
	out, healed, added := HealTrackingGaps(in, gaps, 200, 0)
	if healed != 1 {
		t.Fatalf("healed=%d want 1", healed)
	}
	if added < 1 {
		t.Fatalf("expected bridge points, added=%d out=%+v", added, out)
	}
	for _, a := range out {
		if a.At > 500 && a.At < 1800 && (a.Pos == 99 || a.Pos == 98) {
			t.Fatalf("junk survived: %+v", a)
		}
	}
	if out[0].At != 0 || out[len(out)-1].At != 2100 {
		t.Fatalf("endpoints: %+v", out)
	}
}

func TestHealTrackingGapsKeepsBoundaryAnchors(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 500, Pos: 20},  // gap start — keep
		{At: 1000, Pos: 99}, // interior junk
		{At: 1500, Pos: 80}, // gap end — keep
		{At: 2000, Pos: 100},
	}
	gaps := []TrackingGap{{StartMs: 500, EndMs: 1500}}
	out, healed, _ := HealTrackingGaps(in, gaps, 100, 0)
	if healed < 1 {
		t.Fatalf("healed=%d", healed)
	}
	has500, has1500 := false, false
	for _, a := range out {
		if a.At == 500 && a.Pos == 20 {
			has500 = true
		}
		if a.At == 1500 && a.Pos == 80 {
			has1500 = true
		}
		if a.At == 1000 && a.Pos == 99 {
			t.Fatalf("interior junk kept: %+v", a)
		}
	}
	if !has500 || !has1500 {
		t.Fatalf("boundary anchors lost: 500=%v 1500=%v out=%+v", has500, has1500, out)
	}
}

func TestHealTrackingGapsDoesNotDensifyOutside(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 500, Pos: 80},
		{At: 1000, Pos: 20},
		{At: 1500, Pos: 90},
		{At: 5000, Pos: 10},
		{At: 5100, Pos: 50},
	}
	// Gap only mid — must not invent points in the healthy early stroke.
	gaps := []TrackingGap{{StartMs: 2000, EndMs: 4500}}
	out, healed, added := HealTrackingGaps(in, gaps, 200, 0)
	if healed < 1 || added < 1 {
		t.Fatalf("heal=%d added=%d out=%+v", healed, added, out)
	}
	early := 0
	outsideGapExtra := 0
	for _, a := range out {
		if a.At > 0 && a.At < 1500 {
			early++
		}
		// Healthy span 1500→5000 outside [2000,4500] must stay empty of new points.
		if a.At > 1500 && a.At < 2000 {
			outsideGapExtra++
		}
		if a.At > 4500 && a.At < 5000 {
			outsideGapExtra++
		}
	}
	if early != 2 { // 500 and 1000 only
		t.Fatalf("densified early script: early=%d out=%+v", early, out)
	}
	if outsideGapExtra != 0 {
		t.Fatalf("densified outside gap window: extra=%d out=%+v", outsideGapExtra, out)
	}
}

func TestHealTrackingGapsMergesOverlappingAndInverts(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 10},
		{At: 800, Pos: 95},
		{At: 2000, Pos: 90},
		{At: 2100, Pos: 100},
	}
	gaps := []TrackingGap{
		{StartMs: 1500, EndMs: 400}, // inverted
		{StartMs: 300, EndMs: 900},  // overlaps
	}
	out, healed, _ := HealTrackingGaps(in, gaps, 100, 0)
	if healed < 1 {
		t.Fatalf("healed=%d out=%+v", healed, out)
	}
	for _, a := range out {
		if a.At == 800 && a.Pos == 95 {
			t.Fatalf("junk in merged window survived: %+v", a)
		}
	}
}

func TestHealTrackingGapsRefusesWhenWouldDestroyScript(t *testing.T) {
	in := []Action{{At: 100, Pos: 10}, {At: 200, Pos: 20}, {At: 300, Pos: 30}}
	gaps := []TrackingGap{{StartMs: 0, EndMs: 400}} // everything is strict interior
	out, healed, added := HealTrackingGaps(in, gaps, 50, 0)
	if healed != 0 || added != 0 {
		t.Fatalf("should refuse: healed=%d added=%d", healed, added)
	}
	if len(out) != len(in) {
		t.Fatalf("mutated on refuse: %+v", out)
	}
}

func TestImproveScriptHealTrackingGaps(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 10},
		{At: 800, Pos: 95},
		{At: 1200, Pos: 94},
		{At: 2000, Pos: 90},
		{At: 2100, Pos: 100},
	}
	res, err := ImproveScript(in, ImproveOpts{
		HealTrackingGaps: true,
		TrackingGaps:     []TrackingGap{{StartMs: 400, EndMs: 1600}},
		StepMs:           200,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.WindowsHealed < 1 || !res.ClearTrackingGaps {
		t.Fatalf("heal result: %+v", res)
	}
	if res.PointsAdded < 1 {
		t.Fatalf("expected bridge points: %+v", res)
	}
}

// TestImproveScriptHealPlusFillKeepsHealPointsCount is the Owner-visible
// "Repair / Fill does nothing" regression: Review defaults both checkboxes on.
// When Fill finds no time holes after Heal bridges a tracking_gaps window,
// PointsAdded must still count the heal bridges (was overwritten to 0).
func TestImproveScriptHealPlusFillKeepsHealPointsCount(t *testing.T) {
	in := []Action{
		{At: 0, Pos: 0},
		{At: 100, Pos: 10},
		{At: 800, Pos: 95},
		{At: 1200, Pos: 94},
		{At: 2000, Pos: 90},
		{At: 2100, Pos: 100},
	}
	healOnly, err := ImproveScript(in, ImproveOpts{
		HealTrackingGaps: true,
		TrackingGaps:     []TrackingGap{{StartMs: 400, EndMs: 1600}},
		StepMs:           200,
	})
	if err != nil {
		t.Fatal(err)
	}
	both, err := ImproveScript(in, ImproveOpts{
		HealTrackingGaps: true,
		TrackingGaps:     []TrackingGap{{StartMs: 400, EndMs: 1600}},
		FillGaps:         true, // auto — no long holes remain after heal
		StepMs:           200,
	})
	if err != nil {
		t.Fatal(err)
	}
	if both.WindowsHealed < 1 {
		t.Fatalf("expected heal: %+v", both)
	}
	if both.GapsFilled != 0 {
		t.Fatalf("expected no fill holes, got gaps=%d: %+v", both.GapsFilled, both)
	}
	if both.PointsAdded < 1 || both.PointsAdded < healOnly.PointsAdded {
		t.Fatalf("Fill overwrote heal PointsAdded: healOnly=%d both=%d res=%+v",
			healOnly.PointsAdded, both.PointsAdded, both)
	}
	if both.AfterCount != healOnly.AfterCount {
		t.Fatalf("actions diverged: heal=%d both=%d", healOnly.AfterCount, both.AfterCount)
	}
}

func TestStampTrackingGapsClears(t *testing.T) {
	dir := t.TempDir()
	path := dir + "/clip.funscript"
	body := `{"actions":[{"at":0,"pos":0},{"at":100,"pos":50}],"metadata":{"tracking_gaps":[{"start_ms":10,"end_ms":50}]}}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := StampTrackingGaps(path, nil); err != nil {
		t.Fatal(err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(s.Metadata.TrackingGaps) != 0 {
		t.Fatalf("gaps remain: %+v", s.Metadata.TrackingGaps)
	}
}
