package funscript

import (
	"fmt"
	"math"
	"sort"
)

const (
	// DefaultFillGapMs: fill segments longer than this when MaxGapMs is 0
	// and the Script-Doctor-style auto threshold is smaller.
	DefaultFillGapMs int64 = 800
	// DefaultFillStepMs: spacing between inserted points when no audio Hz hint.
	DefaultFillStepMs int64 = 100
)

// ImproveOpts controls post-generate script polish (FunGen-like end step).
// Mutators only — never invent a full curve from audio.
type ImproveOpts struct {
	// StartMs / EndMs trim the script. 0 = no bound on that side.
	// EndMs < 0 also means no end bound.
	StartMs int64
	EndMs   int64
	// FillGaps inserts linear points across long action gaps.
	FillGaps bool
	// MaxGapMs: fill only when Δt exceeds this. 0 = auto (max(median*8, 2000)
	// or DefaultFillGapMs, whichever is smaller — catches doctor-flagged gaps
	// and shorter tracker holes).
	MaxGapMs int64
	// StepMs between inserted points. 0 = DefaultFillStepMs, or derived from
	// AudioHz when AudioHz > 0 (half-period of the audio tempo).
	StepMs int64
	// AudioHz optional tempo hint for fill spacing (from CheckAudioTempo).
	AudioHz float64
	// HealTrackingGaps strips junk points inside known tracker-loss windows
	// (metadata.tracking_gaps) and linearly re-bridges those ranges. Does not
	// re-run CSRT — classical bridge only. Empty TrackingGaps = no-op.
	HealTrackingGaps bool
	TrackingGaps     []TrackingGap
	// HealRhythm (opt-in): bridge the healed windows with the stroke rhythm
	// around them (RhythmBridgeSpans) instead of the straight line; a window
	// without rhythm on its sides still gets the line.
	HealRhythm bool
	// RepairSpans are user-selected spans ("repair this span"): the points
	// inside are replaced by the rhythm bridge (line when no rhythm around).
	// Empty = no-op.
	RepairSpans []TrackingGap
}

// ImproveResult summarizes what changed.
type ImproveResult struct {
	Actions       []Action `json:"-"`
	BeforeCount   int      `json:"beforeCount"`
	AfterCount    int      `json:"afterCount"`
	Trimmed       bool     `json:"trimmed"`
	GapsFilled    int      `json:"gapsFilled"`
	PointsAdded   int      `json:"pointsAdded"`
	FillGapMs     int64    `json:"fillGapMs"`
	FillStepMs    int64    `json:"fillStepMs"`
	WindowsHealed int      `json:"windowsHealed"`
	// RhythmBridged counts windows / spans filled with the rhythm bridge
	// (the rest of WindowsHealed / SpansRepaired got the straight line).
	RhythmBridged int `json:"rhythmBridged"`
	SpansRepaired int `json:"spansRepaired"`
	// ClearTrackingGaps hints the caller to drop healed windows from metadata
	// so Contact vib is not muted forever on rewritten ranges.
	ClearTrackingGaps bool `json:"clearTrackingGaps"`
}

// TrimActions keeps actions in [startMs, endMs] (inclusive).
// startMs < 0 or endMs < 0 means unbounded on that side.
// Ensures at least two points; interpolates endpoints when the cut falls
// between existing actions.
func TrimActions(actions []Action, startMs, endMs int64) ([]Action, error) {
	if len(actions) < 2 {
		return nil, fmt.Errorf("funscript: trim needs at least 2 points")
	}
	sorted := append([]Action(nil), actions...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })

	lo := sorted[0].At
	hi := sorted[len(sorted)-1].At
	if startMs < 0 {
		startMs = lo
	}
	if endMs < 0 {
		endMs = hi
	}
	if endMs < startMs {
		startMs, endMs = endMs, startMs
	}
	if startMs <= lo && endMs >= hi {
		return sorted, nil
	}

	out := make([]Action, 0, len(sorted)+2)
	// Leading endpoint
	if startMs > lo {
		out = append(out, Action{At: startMs, Pos: clampPos(interpAt(sorted, startMs))})
	}
	for _, a := range sorted {
		if a.At < startMs || a.At > endMs {
			continue
		}
		if len(out) > 0 && out[len(out)-1].At == a.At {
			out[len(out)-1] = a
			continue
		}
		out = append(out, a)
	}
	if endMs < hi {
		ep := Action{At: endMs, Pos: clampPos(interpAt(sorted, endMs))}
		if len(out) == 0 || out[len(out)-1].At != endMs {
			out = append(out, ep)
		} else {
			out[len(out)-1] = ep
		}
	}
	if len(out) < 2 {
		return nil, fmt.Errorf("funscript: trim would leave fewer than 2 points")
	}
	return out, nil
}

// FillGaps inserts linearly interpolated actions across long time gaps.
// Does not invent motion from audio — optional AudioHz only sets step spacing.
func FillGaps(actions []Action, maxGapMs, stepMs int64, audioHz float64) (out []Action, gapsFilled, pointsAdded int) {
	if len(actions) < 2 {
		return append([]Action(nil), actions...), 0, 0
	}
	sorted := append([]Action(nil), actions...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })

	if maxGapMs <= 0 {
		maxGapMs = autoFillGapThreshold(sorted)
	}
	if stepMs <= 0 {
		stepMs = DefaultFillStepMs
		if audioHz > 0.2 && audioHz < 5 {
			// Half-period so a full stroke (up+down) can land near audio tempo.
			half := int64(math.Round(1000.0 / (2.0 * audioHz)))
			if half >= 40 && half <= 800 {
				stepMs = half
			}
		}
	}
	if stepMs < 20 {
		stepMs = 20
	}

	out = make([]Action, 0, len(sorted)*2)
	out = append(out, sorted[0])
	for i := 1; i < len(sorted); i++ {
		prev := out[len(out)-1]
		next := sorted[i]
		dt := next.At - prev.At
		if dt > maxGapMs && dt > stepMs {
			gapsFilled++
			n := int(dt / stepMs)
			for k := 1; k < n; k++ {
				t := prev.At + int64(k)*stepMs
				if t >= next.At {
					break
				}
				frac := float64(t-prev.At) / float64(dt)
				pos := int(math.Round(float64(prev.Pos) + frac*float64(next.Pos-prev.Pos)))
				out = append(out, Action{At: t, Pos: clampPos(pos)})
				pointsAdded++
			}
		}
		if out[len(out)-1].At == next.At {
			out[len(out)-1] = next
		} else {
			out = append(out, next)
		}
	}
	return out, gapsFilled, pointsAdded
}

// HealTrackingGaps strips actions strictly inside known tracker-loss windows
// and linearly re-bridges only those windows (does not densify outside them).
// Callers should clear the healed windows from script metadata afterwards.
func HealTrackingGaps(actions []Action, gaps []TrackingGap, stepMs int64, audioHz float64) (out []Action, windowsHealed, pointsAdded int) {
	if len(actions) < 2 || len(gaps) == 0 {
		return append([]Action(nil), actions...), 0, 0
	}
	merged := mergeTrackingGaps(gaps)
	if len(merged) == 0 {
		return append([]Action(nil), actions...), 0, 0
	}
	sorted := append([]Action(nil), actions...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })

	// Keep boundary points at StartMs/EndMs as anchors — only drop the
	// strict interior (junk while the tracker was lost).
	kept := make([]Action, 0, len(sorted))
	removed := 0
	for _, a := range sorted {
		if insideTrackingGapStrict(a.At, merged) {
			removed++
			continue
		}
		kept = append(kept, a)
	}
	if len(kept) < 2 {
		// Healing would destroy the script — refuse silently (caller keeps original).
		return append([]Action(nil), actions...), 0, 0
	}

	step := stepOrDefault(stepMs, audioHz)
	if step < 20 {
		step = 20
	}

	touched := make([]bool, len(merged))
	out = make([]Action, 0, len(kept)+len(merged)*8)
	out = append(out, kept[0])
	for i := 1; i < len(kept); i++ {
		prev := out[len(out)-1]
		next := kept[i]
		dt := next.At - prev.At
		if dt > step {
			for k := 1; ; k++ {
				t := prev.At + int64(k)*step
				if t >= next.At {
					break
				}
				g := gapIndexContaining(t, merged)
				if g < 0 {
					// Outside every loss window — leave the healthy curve alone.
					continue
				}
				frac := float64(t-prev.At) / float64(dt)
				pos := int(math.Round(float64(prev.Pos) + frac*float64(next.Pos-prev.Pos)))
				out = append(out, Action{At: t, Pos: clampPos(pos)})
				pointsAdded++
				touched[g] = true
			}
		}
		// Mark gaps whose interior we stripped even when no new points fit.
		if removed > 0 {
			for g, gap := range merged {
				if prev.At <= gap.EndMs && next.At >= gap.StartMs {
					if removedInGap(sorted, gap) {
						touched[g] = true
					}
				}
			}
		}
		if out[len(out)-1].At == next.At {
			out[len(out)-1] = next
		} else {
			out = append(out, next)
		}
	}
	for _, t := range touched {
		if t {
			windowsHealed++
		}
	}
	if windowsHealed == 0 && removed == 0 && pointsAdded == 0 {
		return append([]Action(nil), actions...), 0, 0
	}
	if windowsHealed == 0 && removed > 0 {
		windowsHealed = len(merged)
	}
	return out, windowsHealed, pointsAdded
}

func mergeTrackingGaps(gaps []TrackingGap) []TrackingGap {
	clean := make([]TrackingGap, 0, len(gaps))
	for _, g := range gaps {
		if g.EndMs < g.StartMs {
			g.StartMs, g.EndMs = g.EndMs, g.StartMs
		}
		if g.EndMs <= g.StartMs {
			continue
		}
		clean = append(clean, g)
	}
	if len(clean) == 0 {
		return nil
	}
	sort.SliceStable(clean, func(i, j int) bool {
		if clean[i].StartMs == clean[j].StartMs {
			return clean[i].EndMs < clean[j].EndMs
		}
		return clean[i].StartMs < clean[j].StartMs
	})
	out := []TrackingGap{clean[0]}
	for _, g := range clean[1:] {
		prev := &out[len(out)-1]
		if g.StartMs <= prev.EndMs {
			if g.EndMs > prev.EndMs {
				prev.EndMs = g.EndMs
			}
			continue
		}
		out = append(out, g)
	}
	return out
}

// insideTrackingGapStrict is true for the open interval (start, end) — boundary
// timestamps stay as anchors.
func insideTrackingGapStrict(at int64, gaps []TrackingGap) bool {
	for _, g := range gaps {
		if at > g.StartMs && at < g.EndMs {
			return true
		}
	}
	return false
}

func gapIndexContaining(at int64, gaps []TrackingGap) int {
	for i, g := range gaps {
		if at >= g.StartMs && at <= g.EndMs {
			return i
		}
	}
	return -1
}

func removedInGap(original []Action, gap TrackingGap) bool {
	for _, a := range original {
		if a.At > gap.StartMs && a.At < gap.EndMs {
			return true
		}
	}
	return false
}

func stepOrDefault(stepMs int64, audioHz float64) int64 {
	if stepMs > 0 {
		return stepMs
	}
	step := DefaultFillStepMs
	if audioHz > 0.2 && audioHz < 5 {
		half := int64(math.Round(1000.0 / (2.0 * audioHz)))
		if half >= 40 && half <= 800 {
			step = half
		}
	}
	return step
}

// ImproveScript applies trim, optional tracking-gap heal, then fill-gaps.
// Order: cut ends → rewrite known loss windows → bridge remaining holes.
func ImproveScript(actions []Action, opts ImproveOpts) (ImproveResult, error) {
	res := ImproveResult{BeforeCount: len(actions)}
	if len(actions) < 2 {
		return res, fmt.Errorf("funscript: improve needs at least 2 points")
	}
	cur := append([]Action(nil), actions...)

	startMs, endMs := opts.StartMs, opts.EndMs
	doTrim := startMs > 0 || endMs > 0
	if doTrim {
		if startMs <= 0 {
			startMs = -1
		}
		if endMs <= 0 {
			endMs = -1
		}
		trimmed, err := TrimActions(cur, startMs, endMs)
		if err != nil {
			return res, err
		}
		if len(trimmed) != len(cur) ||
			(len(trimmed) > 0 && len(cur) > 0 &&
				(trimmed[0].At != cur[0].At || trimmed[len(trimmed)-1].At != cur[len(cur)-1].At)) {
			res.Trimmed = true
		}
		cur = trimmed
	}

	if opts.HealTrackingGaps && len(opts.TrackingGaps) > 0 {
		if opts.HealRhythm {
			healed, nRhythm, nLine, nPts := healSpansRhythm(cur, opts.TrackingGaps, opts.StepMs, opts.AudioHz)
			if nRhythm+nLine > 0 {
				cur = healed
				res.WindowsHealed = nRhythm + nLine
				res.RhythmBridged += nRhythm
				res.PointsAdded += nPts
				res.ClearTrackingGaps = true
			}
		} else {
			healed, nWin, nPts := HealTrackingGaps(cur, opts.TrackingGaps, opts.StepMs, opts.AudioHz)
			if nWin > 0 {
				cur = healed
				res.WindowsHealed = nWin
				res.PointsAdded += nPts
				res.ClearTrackingGaps = true
			}
		}
	}

	if len(opts.RepairSpans) > 0 {
		repaired, nRhythm, nLine, nPts := healSpansRhythm(cur, opts.RepairSpans, opts.StepMs, opts.AudioHz)
		cur = repaired
		res.SpansRepaired = nRhythm + nLine
		res.RhythmBridged += nRhythm
		res.PointsAdded += nPts
	}

	if opts.FillGaps {
		maxGap := opts.MaxGapMs
		if maxGap <= 0 {
			maxGap = autoFillGapThreshold(cur)
		}
		step := opts.StepMs
		// Median of the *original* spacing — after filling long holes with
		// stepMs points the median collapses and must not drive a second pass.
		origMed := medianSpacingMs(cur)
		filled, nGaps, nPts := FillGaps(cur, maxGap, step, opts.AudioHz)
		// Auto mode: second pass only for medium outliers below the first
		// threshold. Floor 400ms, but never below ~4× natural stroke spacing
		// (slow strokes must not get densified into 100ms linear ramps).
		if opts.MaxGapMs <= 0 {
			tight := DefaultFillGapMs / 2 // 400ms
			if origMed > 0 {
				rel := int64(math.Round(origMed * 4))
				if rel > tight {
					tight = rel
				}
			}
			if tight > 0 && tight < maxGap {
				var g2, p2 int
				filled, g2, p2 = FillGaps(filled, tight, step, opts.AudioHz)
				nGaps += g2
				nPts += p2
				maxGap = tight
			}
		}
		res.GapsFilled = nGaps
		// Keep heal bridge points in the total — assigning nPts alone made the
		// default Heal+Fill path report PointsAdded=0 whenever Fill found no
		// time holes (typical after Generate), so the GUI said "gaps ok" even
		// though Heal rewrote tracking_gaps windows.
		res.PointsAdded += nPts
		res.FillGapMs = maxGap
		if step <= 0 {
			step = DefaultFillStepMs
			if opts.AudioHz > 0.2 && opts.AudioHz < 5 {
				half := int64(math.Round(1000.0 / (2.0 * opts.AudioHz)))
				if half >= 40 && half <= 800 {
					step = half
				}
			}
		}
		res.FillStepMs = step
		cur = filled
	}

	res.Actions = cur
	res.AfterCount = len(cur)
	return res, nil
}

func autoFillGapThreshold(actions []Action) int64 {
	var dts []float64
	for i := 1; i < len(actions); i++ {
		d := float64(actions[i].At - actions[i-1].At)
		if d > 0 {
			dts = append(dts, d)
		}
	}
	if len(dts) == 0 {
		return DefaultFillGapMs
	}
	med := medianFloat(dts)
	doctor := int64(math.Max(med*8, 2000))
	if doctor < DefaultFillGapMs {
		return doctor
	}
	// Prefer filling shorter tracker holes than the doctor threshold alone.
	return DefaultFillGapMs
}

func medianSpacingMs(actions []Action) float64 {
	var dts []float64
	for i := 1; i < len(actions); i++ {
		d := float64(actions[i].At - actions[i-1].At)
		if d > 0 {
			dts = append(dts, d)
		}
	}
	if len(dts) == 0 {
		return 0
	}
	return medianFloat(dts)
}

func interpAt(actions []Action, t int64) int {
	if len(actions) == 0 {
		return 50
	}
	if t <= actions[0].At {
		return actions[0].Pos
	}
	last := actions[len(actions)-1]
	if t >= last.At {
		return last.Pos
	}
	for i := 1; i < len(actions); i++ {
		a, b := actions[i-1], actions[i]
		if t >= a.At && t <= b.At {
			if b.At == a.At {
				return b.Pos
			}
			frac := float64(t-a.At) / float64(b.At-a.At)
			return int(math.Round(float64(a.Pos) + frac*float64(b.Pos-a.Pos)))
		}
	}
	return last.Pos
}

func clampPos(p int) int {
	if p < 0 {
		return 0
	}
	if p > 100 {
		return 100
	}
	return p
}
