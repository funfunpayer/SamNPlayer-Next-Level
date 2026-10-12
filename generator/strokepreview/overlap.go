package strokepreview

import (
	"math"
	"sort"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator/posttrack"
)

// PeakOverlap scores how many preview "up" extrema land within tolMs of a
// reference funscript peak (high-position local max on the action curve).
// Directional Stage-A gate — not Motion Fidelity.
func PeakOverlap(rep Report, ref []funscript.Action, tolMs int) (matched, refPeaks int, recall float64) {
	if tolMs <= 0 {
		tolMs = 250
	}
	refTimes := funscriptPeakTimes(ref)
	refPeaks = len(refTimes)
	if refPeaks == 0 || rep.UpCount == 0 {
		return 0, refPeaks, 0
	}
	ups := make([]int, 0, rep.UpCount)
	for _, e := range rep.Extrema {
		if e.Kind == "up" {
			ups = append(ups, e.AtMs)
		}
	}
	used := make([]bool, len(refTimes))
	for _, t := range ups {
		best := -1
		bestAbs := tolMs + 1
		for i, rt := range refTimes {
			if used[i] {
				continue
			}
			d := int(math.Abs(float64(t - rt)))
			if d <= tolMs && d < bestAbs {
				bestAbs = d
				best = i
			}
		}
		if best >= 0 {
			used[best] = true
			matched++
		}
	}
	recall = float64(matched) / float64(refPeaks)
	return matched, refPeaks, recall
}

func funscriptPeakTimes(actions []funscript.Action) []int {
	if len(actions) < 3 {
		return nil
	}
	// Resample to 50ms for peak detect.
	step := 50
	start := int(actions[0].At)
	end := int(actions[len(actions)-1].At)
	if end <= start {
		return nil
	}
	n := (end-start)/step + 1
	pos := make([]float64, n)
	j := 0
	for i := 0; i < n; i++ {
		t := start + i*step
		for j+1 < len(actions) && int(actions[j+1].At) <= t {
			j++
		}
		if j+1 < len(actions) {
			a0, a1 := actions[j], actions[j+1]
			span := float64(a1.At - a0.At)
			if span <= 0 {
				pos[i] = float64(a0.Pos)
			} else {
				u := float64(t-int(a0.At)) / span
				pos[i] = float64(a0.Pos)*(1-u) + float64(a1.Pos)*u
			}
		} else {
			pos[i] = float64(actions[j].Pos)
		}
	}
	span := peakToPeak(pos)
	prom := span * 0.15
	idx := posttrack.FindPeaks(pos, int(math.Round(200.0/float64(step))), prom)
	out := make([]int, len(idx))
	for i, k := range idx {
		out[i] = start + k*step
	}
	sort.Ints(out)
	return out
}
