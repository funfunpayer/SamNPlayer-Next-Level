package motionx

import "math"

// State is a coarse description of what the motion is doing over a stretch of
// time. It says nothing about what is moving, only how.
type State string

const (
	Static       State = "static"
	Starting     State = "starting"
	Accelerating State = "accelerating"
	Regular      State = "regular"
	Decelerating State = "decelerating"
	Stopping     State = "stopping"
)

// Segment is a contiguous run of one state.
type Segment struct {
	State   State
	StartMs float64
	EndMs   float64
	// MeanSpeed is the average absolute speed in position units per second.
	MeanSpeed float64
	Samples   int
}

// Duration returns the segment length in milliseconds.
func (s Segment) Duration() float64 { return s.EndMs - s.StartMs }

// ClassifyOptions tunes the thresholds.
type ClassifyOptions struct {
	// StaticSpeed is the speed below which motion counts as standstill,
	// in position units per second. Default 4.
	StaticSpeed float64
	// ChangeRatio is the relative speed change needed to call a sample
	// accelerating or decelerating. Default 0.25 (25 percent).
	ChangeRatio float64
	// MinSegmentMs merges runs shorter than this into their predecessor.
	// Default 250.
	MinSegmentMs float64
	// SmoothRadius averages the speed estimate over +/- this many samples
	// before classifying. Default 2. Set to 0 to disable.
	SmoothRadius int
}

func (o *ClassifyOptions) applyDefaults() {
	if o.StaticSpeed <= 0 {
		o.StaticSpeed = 4
	}
	if o.ChangeRatio <= 0 {
		o.ChangeRatio = 0.25
	}
	if o.MinSegmentMs <= 0 {
		o.MinSegmentMs = 250
	}
	if o.SmoothRadius < 0 {
		o.SmoothRadius = 0
	}
}

// Classify segments a curve into motion states.
//
// Differences to the original state classifier:
//   - speed is derived from the curve itself instead of requiring a
//     pre-populated Velocity field, and it is expressed per second, so the
//     thresholds do not silently change meaning when the sample rate changes
//   - acceleration is a relative change, not an absolute one, so a slow and a
//     fast passage are judged on the same footing
//   - short segments merge into the neighbour they resemble rather than always
//     into the predecessor, and the merge no longer swallows a leading segment
func Classify(pts []Point, opt ClassifyOptions) []Segment {
	opt.applyDefaults()
	if len(pts) < 2 {
		return nil
	}

	speed := absSpeed(pts)
	if opt.SmoothRadius > 0 {
		speed = movingAverage(speed, opt.SmoothRadius)
	}

	states := make([]State, len(speed))
	for i := range speed {
		states[i] = classifyAt(speed, i, opt)
	}

	segs := runs(pts, speed, states)
	return mergeShort(segs, opt.MinSegmentMs)
}

func classifyAt(speed []float64, i int, opt ClassifyOptions) State {
	v := speed[i]
	if i == 0 {
		if v <= opt.StaticSpeed {
			return Static
		}
		return Starting
	}
	prev := speed[i-1]

	if v <= opt.StaticSpeed {
		if prev > opt.StaticSpeed {
			return Stopping
		}
		return Static
	}
	if prev <= opt.StaticSpeed {
		return Starting
	}

	// Both above the static floor: compare magnitudes relatively.
	ref := math.Max(prev, 1e-9)
	change := (v - prev) / ref
	switch {
	case change > opt.ChangeRatio:
		return Accelerating
	case change < -opt.ChangeRatio:
		return Decelerating
	default:
		return Regular
	}
}

func runs(pts []Point, speed []float64, states []State) []Segment {
	var out []Segment
	start := 0
	for i := 1; i <= len(states); i++ {
		if i < len(states) && states[i] == states[start] {
			continue
		}
		var sum float64
		for j := start; j < i; j++ {
			sum += speed[j]
		}
		out = append(out, Segment{
			State:     states[start],
			StartMs:   pts[start].TMs,
			EndMs:     pts[i-1].TMs,
			MeanSpeed: sum / float64(i-start),
			Samples:   i - start,
		})
		start = i
	}
	return out
}

func mergeShort(in []Segment, minMs float64) []Segment {
	if len(in) < 2 {
		return in
	}
	out := append([]Segment(nil), in...)

	for {
		idx := -1
		for i, s := range out {
			if s.Duration() < minMs && len(out) > 1 {
				idx = i
				break
			}
		}
		if idx < 0 {
			return out
		}

		// Merge into whichever neighbour has the closer mean speed.
		target := idx - 1
		if idx == 0 {
			target = 1
		} else if idx+1 < len(out) {
			dPrev := math.Abs(out[idx].MeanSpeed - out[idx-1].MeanSpeed)
			dNext := math.Abs(out[idx].MeanSpeed - out[idx+1].MeanSpeed)
			if dNext < dPrev {
				target = idx + 1
			}
		}

		a, b := idx, target
		if b < a {
			a, b = b, a
		}
		merged := Segment{
			State:   out[b].State,
			StartMs: out[a].StartMs,
			EndMs:   out[b].EndMs,
			Samples: out[a].Samples + out[b].Samples,
		}
		if out[a].Samples >= out[b].Samples {
			merged.State = out[a].State
		}
		merged.MeanSpeed = (out[a].MeanSpeed*float64(out[a].Samples) +
			out[b].MeanSpeed*float64(out[b].Samples)) / float64(merged.Samples)

		next := append([]Segment{}, out[:a]...)
		next = append(next, merged)
		next = append(next, out[b+1:]...)
		out = next
	}
}

// absSpeed returns |dPos/dt| per second, one value per sample.
func absSpeed(pts []Point) []float64 {
	out := make([]float64, len(pts))
	for i := 1; i < len(pts); i++ {
		dt := (pts[i].TMs - pts[i-1].TMs) / 1000
		if dt <= 0 {
			out[i] = out[i-1]
			continue
		}
		out[i] = math.Abs(pts[i].Pos-pts[i-1].Pos) / dt
	}
	if len(out) > 1 {
		out[0] = out[1]
	}
	return out
}

func movingAverage(v []float64, radius int) []float64 {
	if radius <= 0 || len(v) < 3 {
		return v
	}
	out := make([]float64, len(v))
	for i := range v {
		lo, hi := i-radius, i+radius
		if lo < 0 {
			lo = 0
		}
		if hi >= len(v) {
			hi = len(v) - 1
		}
		var sum float64
		for j := lo; j <= hi; j++ {
			sum += v[j]
		}
		out[i] = sum / float64(hi-lo+1)
	}
	return out
}
