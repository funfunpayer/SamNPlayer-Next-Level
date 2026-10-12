package funscript

import (
	"math"
	"sort"
)

// DefaultRhythmContextMs is how far RhythmBridgeSpans looks on each side of
// a span for the stroke tempo and levels.
const DefaultRhythmContextMs int64 = 8000

// rhythmTurnMinAmp: a direction change counts as a stroke turning point only
// after this much travel (0-100 scale), so dense frame-level scripts and
// sparse extrema scripts read the same.
const rhythmTurnMinAmp = 8

// RhythmBridgeSpans replaces the actions strictly inside each span with a
// continuation of the stroke rhythm around it, instead of the straight line
// HealTrackingGaps draws: turning points at the half-period measured in the
// ctxMs before and after the span, high/low levels from those turning
// points, and the count fitted so the alternation lands on the first point
// after the span. A span without enough rhythm on its sides (fewer than two
// turning points in the context) is left untouched and not counted.
//
// Synthetic gaps cut from the FunGen reference scripts (docs/
// SCENE_UNDERSTANDING_PLAN.md §3b): r against the uncut original -0.11 ->
// 0.43 (clip_voll, 4 s), 0.01 -> 0.93 (clip_ausschnitt, 6 s) vs the line.
func RhythmBridgeSpans(actions []Action, spans []TrackingGap, ctxMs int64) (out []Action, spansFilled, pointsAdded int) {
	cur := append([]Action(nil), actions...)
	sort.SliceStable(cur, func(i, j int) bool { return cur[i].At < cur[j].At })
	if len(cur) < 2 || len(spans) == 0 {
		return cur, 0, 0
	}
	if ctxMs <= 0 {
		ctxMs = DefaultRhythmContextMs
	}
	for _, g := range mergeTrackingGaps(spans) {
		next, added, ok := rhythmBridgeOne(cur, g.StartMs, g.EndMs, ctxMs)
		if ok {
			cur = next
			spansFilled++
			pointsAdded += added
		}
	}
	return cur, spansFilled, pointsAdded
}

func rhythmBridgeOne(sorted []Action, s, e, ctxMs int64) ([]Action, int, bool) {
	// Anchors: last point at or before s, first at or after e.
	ia, ib := -1, -1
	for i, a := range sorted {
		if a.At <= s {
			ia = i
		}
		if a.At >= e && ib < 0 {
			ib = i
		}
	}
	if ia < 0 || ib < 0 || ib <= ia {
		return sorted, 0, false
	}
	var left, right []Action
	for _, a := range sorted {
		if a.At >= s-ctxMs && a.At <= s {
			left = append(left, a)
		} else if a.At >= e && a.At <= e+ctxMs {
			right = append(right, a)
		}
	}
	var dts, his, los []float64
	turns := 0
	for _, side := range [][]Action{rhythmTurningPoints(left), rhythmTurningPoints(right)} {
		turns += len(side)
		for i := 1; i < len(side); i++ {
			dts = append(dts, float64(side[i].At-side[i-1].At))
			if side[i].Pos > side[i-1].Pos {
				his = append(his, float64(side[i].Pos))
			} else {
				los = append(los, float64(side[i].Pos))
			}
		}
	}
	if turns < 2 || len(dts) == 0 || len(his) == 0 || len(los) == 0 {
		return sorted, 0, false
	}
	half, hi, lo := medianFloat(dts), medianFloat(his), medianFloat(los)
	if half <= 0 || hi <= lo {
		return sorted, 0, false
	}
	a0, b0 := sorted[ia], sorted[ib]
	mid := (hi + lo) / 2
	aHigh, bHigh := float64(a0.Pos) >= mid, float64(b0.Pos) >= mid
	span := float64(b0.At - a0.At)
	n := int(math.Round(span / half))
	if n < 1 {
		n = 1
	}
	// n half-periods flip the level n times; it must land on b0's level.
	if (aHigh != (n%2 == 1)) != bHigh {
		up, down := span/float64(n+1), span/float64(max(n-1, 1))
		if n == 1 || math.Abs(up-half) < math.Abs(down-half) {
			n++
		} else {
			n--
		}
	}
	out := make([]Action, 0, len(sorted)+n)
	out = append(out, sorted[:ia+1]...)
	high := aHigh
	for k := 1; k < n; k++ {
		high = !high
		pos := lo
		if high {
			pos = hi
		}
		out = append(out, Action{At: a0.At + int64(math.Round(float64(k)*span/float64(n))), Pos: clampPos(int(math.Round(pos)))})
	}
	out = append(out, sorted[ib:]...)
	return out, n - 1, true
}

// rhythmTurningPoints returns the direction changes of a time-sorted run
// that travelled at least rhythmTurnMinAmp.
func rhythmTurningPoints(run []Action) []Action {
	var out []Action
	if len(run) < 2 {
		return out
	}
	cand, dir := run[0], 0
	for _, a := range run[1:] {
		switch {
		case dir >= 0 && a.Pos >= cand.Pos:
			cand, dir = a, 1
		case dir <= 0 && a.Pos <= cand.Pos:
			cand, dir = a, -1
		case absInt(a.Pos-cand.Pos) >= rhythmTurnMinAmp:
			out = append(out, cand)
			cand, dir = a, -dir
		}
	}
	return out
}

// healSpansRhythm bridges each span with the rhythm around it and falls back
// to the straight-line heal for spans without enough rhythm on their sides.
func healSpansRhythm(actions []Action, spans []TrackingGap, stepMs int64, audioHz float64) (out []Action, nRhythm, nLine, points int) {
	cur := append([]Action(nil), actions...)
	sort.SliceStable(cur, func(i, j int) bool { return cur[i].At < cur[j].At })
	var lineSpans []TrackingGap
	for _, g := range mergeTrackingGaps(spans) {
		next, added, ok := rhythmBridgeOne(cur, g.StartMs, g.EndMs, DefaultRhythmContextMs)
		if ok {
			cur = next
			nRhythm++
			points += added
		} else {
			lineSpans = append(lineSpans, g)
		}
	}
	if len(lineSpans) > 0 {
		healed, n, pts := HealTrackingGaps(cur, lineSpans, stepMs, audioHz)
		cur, nLine, points = healed, n, points+pts
	}
	return cur, nRhythm, nLine, points
}
