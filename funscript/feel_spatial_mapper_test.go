package funscript

import "testing"

// Feel-decouple Stage A: shallow stroke but tip over contact mark → vib on.
func TestSpatialFeelBoostsShallowStroke(t *testing.T) {
	s := &Script{
		Actions: []Action{
			{At: 0, Pos: 20},
			{At: 1000, Pos: 30}, // shallow — below default contact depth slice
			{At: 2000, Pos: 20},
		},
	}
	s.Metadata.ContactMarks = &ContactMarks{
		Primary: &ContactMarkBox{X: 40, Y: 40, W: 40, H: 40},
	}
	s.Metadata.Trajectory = &TrajectoryData{
		Tip: []TrajectoryPoint{
			{AtMs: 0, X: 0, Y: 0},
			{AtMs: 1000, X: 60, Y: 60},
			{AtMs: 2000, X: 0, Y: 0},
		},
	}
	opts := DefaultMapOptions()
	opts.TickMs = 50
	opts.ContactVibration = true
	opts.ContactVibrationSpan = 0.75
	opts.ContactVibrationEnvelope = -1
	opts.Sync = SyncIndependent

	frames := s.ToIntensityCurve(opts)
	var mid, edge float64
	for _, f := range frames {
		if f.At == 1000 {
			mid = f.Vibration
		}
		if f.At == 0 {
			edge = f.Vibration
		}
	}
	if mid < 0.5 {
		t.Fatalf("expected spatial vib at tip-on-mark: mid=%v (edge=%v)", mid, edge)
	}
	if edge > mid {
		t.Fatalf("edge vib should not exceed mid: edge=%v mid=%v", edge, mid)
	}
}

// S2 prefer-spatial: with marks+trajectory, deep stroke away from marks must
// not keep full depth envelope (attenuated by SpatialDepthWeight).
func TestPreferSpatialAttenuatesDepthAwayFromMark(t *testing.T) {
	mk := func(withSpatial bool) *Script {
		s := &Script{
			Actions: []Action{
				{At: 0, Pos: 20},
				{At: 500, Pos: 90}, // deep peak
				{At: 1000, Pos: 20},
			},
		}
		if withSpatial {
			s.Metadata.ContactMarks = &ContactMarks{
				Primary: &ContactMarkBox{X: 40, Y: 40, W: 40, H: 40},
			}
			// Tip stays far from the mark the whole time.
			s.Metadata.Trajectory = &TrajectoryData{
				Tip: []TrajectoryPoint{
					{AtMs: 0, X: 0, Y: 0},
					{AtMs: 500, X: 0, Y: 0},
					{AtMs: 1000, X: 0, Y: 0},
				},
			}
		}
		return s
	}
	opts := DefaultMapOptions()
	opts.TickMs = 50
	opts.ContactVibration = true
	opts.ContactVibrationSpan = 0.75
	opts.ContactVibrationEnvelope = -1
	opts.Sync = SyncIndependent
	opts.MinVibration = 0

	vibAt := func(s *Script, at int64) float64 {
		for _, f := range s.ToIntensityCurve(opts) {
			if f.At == at {
				return f.Vibration
			}
		}
		t.Fatalf("no frame at %dms", at)
		return 0
	}
	depthOnly := vibAt(mk(false), 500)
	preferSpatial := vibAt(mk(true), 500)
	if depthOnly < 0.8 {
		t.Fatalf("depth-only deep peak should be strong, got %.3f", depthOnly)
	}
	want := depthOnly * SpatialDepthWeight
	if preferSpatial > want+0.05 || preferSpatial < want-0.05 {
		t.Fatalf("S2 should attenuate depth when tip away from marks: got %.3f want ~%.3f (full=%.3f)",
			preferSpatial, want, depthOnly)
	}
}

// S2: spatial graze still reaches full contact intensity (not attenuated).
func TestPreferSpatialKeepsSpatialPeak(t *testing.T) {
	s := &Script{
		Actions: []Action{
			{At: 0, Pos: 20},
			{At: 1000, Pos: 25},
			{At: 2000, Pos: 20},
		},
	}
	s.Metadata.ContactMarks = &ContactMarks{
		Primary: &ContactMarkBox{X: 40, Y: 40, W: 40, H: 40},
	}
	s.Metadata.Trajectory = &TrajectoryData{
		Tip: []TrajectoryPoint{
			{AtMs: 0, X: 0, Y: 0},
			{AtMs: 1000, X: 60, Y: 60},
			{AtMs: 2000, X: 0, Y: 0},
		},
	}
	opts := DefaultMapOptions()
	opts.TickMs = 50
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = -1
	opts.Sync = SyncIndependent
	opts.MinVibration = 0

	var mid float64
	for _, f := range s.ToIntensityCurve(opts) {
		if f.At == 1000 {
			mid = f.Vibration
		}
	}
	if mid < 0.9 {
		t.Fatalf("spatial hit must stay strong under prefer-spatial: mid=%.3f", mid)
	}
}

// S2: spatial hits use a snappier envelope than the default depth smooth.
func TestPreferSpatialShortensEnvelopeOnHit(t *testing.T) {
	s := &Script{
		Actions: []Action{
			{At: 0, Pos: 20},
			{At: 2000, Pos: 20},
		},
	}
	s.Metadata.ContactMarks = &ContactMarks{
		Primary: &ContactMarkBox{X: 40, Y: 40, W: 40, H: 40},
	}
	// Single-frame graze at t=1000 then tip leaves.
	s.Metadata.Trajectory = &TrajectoryData{
		Tip: []TrajectoryPoint{
			{AtMs: 0, X: 0, Y: 0},
			{AtMs: 1000, X: 60, Y: 60},
			{AtMs: 1050, X: 0, Y: 0},
			{AtMs: 2000, X: 0, Y: 0},
		},
	}
	opts := DefaultMapOptions()
	opts.TickMs = 50
	opts.ContactVibration = true
	opts.ContactVibrationEnvelope = DefaultContactEnvelopeSmooth // 0.45
	opts.Sync = SyncIndependent
	opts.MinVibration = 0
	opts.Smoothing = 0

	var peak, after float64
	for _, f := range s.ToIntensityCurve(opts) {
		if f.At == 1000 {
			peak = f.Vibration
		}
		if f.At == 1200 {
			after = f.Vibration
		}
	}
	if peak < 0.5 {
		t.Fatalf("expected spatial peak, got %.3f", peak)
	}
	// With SpatialContactEnvelopeSmooth=0.18, vib decays faster than 0.45.
	// After 200ms (4 ticks) residual should be well below half the peak.
	if after > peak*0.55 {
		t.Fatalf("spatial envelope should snap off: peak=%.3f after200ms=%.3f", peak, after)
	}
}
