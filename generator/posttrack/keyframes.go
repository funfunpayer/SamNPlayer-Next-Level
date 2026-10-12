package posttrack

import (
	"container/heap"
	"sort"

	"github.com/funfunpayer/SamNPlayer/motionx"
)

// refineKeyframes adds points where linear interpolation between extrema
// would miss the curve — port of generate_funscript.refine_keyframes.
func refineKeyframes(timestampsMs, pos []float64, baseIdx []int, maxError, maxExtraRatio float64) []int {
	if maxError <= 0 || len(baseIdx) < 2 {
		return append([]int(nil), baseIdx...)
	}
	if maxExtraRatio <= 0 {
		maxExtraRatio = 2.0
	}
	limit := int(float64(len(baseIdx)) * maxExtraRatio)
	result := uniqueSorted(baseIdx)

	h := &refineHeap{}
	heap.Init(h)

	worstPoint := func(start, end int) (dev float64, split int, ok bool) {
		if end-start < 2 {
			return 0, 0, false
		}
		span := timestampsMs[end] - timestampsMs[start]
		if span <= 0 {
			return 0, 0, false
		}
		maxDev := -1.0
		maxIdx := -1
		p0, p1 := pos[start], pos[end]
		t0 := timestampsMs[start]
		for i := start + 1; i < end; i++ {
			line := p0 + (p1-p0)*(timestampsMs[i]-t0)/span
			d := abs(pos[i] - line)
			if d > maxDev {
				maxDev = d
				maxIdx = i
			}
		}
		if maxIdx < 0 || maxDev <= maxError {
			return 0, 0, false
		}
		return maxDev, maxIdx, true
	}

	for i := 0; i < len(result)-1; i++ {
		if dev, split, ok := worstPoint(result[i], result[i+1]); ok {
			heap.Push(h, refineItem{-dev, result[i], result[i+1], split})
		}
	}

	added := map[int]bool{}
	for h.Len() > 0 && len(result)+len(added) < limit {
		it := heap.Pop(h).(refineItem)
		added[it.split] = true
		for _, pair := range [][2]int{{it.start, it.split}, {it.split, it.end}} {
			if dev, split, ok := worstPoint(pair[0], pair[1]); ok {
				heap.Push(h, refineItem{-dev, pair[0], pair[1], split})
			}
		}
	}

	for k := range added {
		result = append(result, k)
	}
	sort.Ints(result)
	return uniqueSorted(result)
}

type refineItem struct {
	priority float64 // negative deviation → min-heap acts as max-heap
	start    int
	end      int
	split    int
}

type refineHeap []refineItem

func (h refineHeap) Len() int           { return len(h) }
func (h refineHeap) Less(i, j int) bool { return h[i].priority < h[j].priority }
func (h refineHeap) Swap(i, j int)      { h[i], h[j] = h[j], h[i] }
func (h *refineHeap) Push(x any)        { *h = append(*h, x.(refineItem)) }
func (h *refineHeap) Pop() any {
	old := *h
	n := len(old)
	it := old[n-1]
	*h = old[:n-1]
	return it
}

// enforceMinInterval removes the less important of any two keyframes closer
// than minIntervalMs — port of generate_funscript.enforce_min_interval.
func enforceMinInterval(timestampsMs, pos []float64, keyframeIdx []int, minIntervalMs float64) []int {
	if minIntervalMs <= 0 || len(keyframeIdx) < 3 {
		return append([]int(nil), keyframeIdx...)
	}
	result := append([]int(nil), keyframeIdx...)

	importance := func(position int) float64 {
		if position <= 0 || position >= len(result)-1 {
			return mathInf
		}
		left, middle, right := result[position-1], result[position], result[position+1]
		span := timestampsMs[right] - timestampsMs[left]
		if span <= 0 {
			return 0
		}
		expected := pos[left] + (pos[right]-pos[left])*(timestampsMs[middle]-timestampsMs[left])/span
		return abs(pos[middle] - expected)
	}

	changed := true
	for changed && len(result) > 2 {
		changed = false
		for i := 0; i < len(result)-1; i++ {
			if timestampsMs[result[i+1]]-timestampsMs[result[i]] >= minIntervalMs {
				continue
			}
			drop := i
			if importance(i) > importance(i+1) {
				drop = i + 1
			}
			if drop == 0 || drop == len(result)-1 {
				if i == 0 {
					drop = i + 1
				} else {
					drop = i
				}
			}
			if drop == 0 || drop == len(result)-1 {
				continue
			}
			result = append(result[:drop], result[drop+1:]...)
			changed = true
			break
		}
	}
	return result
}

const mathInf = 1e300

// rdpSimplify keeps keyframe indices using vertical-distance RDP via motionx.
func rdpSimplify(timestampsMs, pos []float64, keyframeIdx []int, epsilon float64) []int {
	if epsilon <= 0 || len(keyframeIdx) < 3 {
		return append([]int(nil), keyframeIdx...)
	}
	pts := make([]motionx.Point, len(keyframeIdx))
	for i, idx := range keyframeIdx {
		pts[i] = motionx.Point{TMs: timestampsMs[idx], Pos: pos[idx]}
	}
	keepLocal := motionx.Simplify(pts, epsilon)
	out := make([]int, len(keepLocal))
	for i, li := range keepLocal {
		out[i] = keyframeIdx[li]
	}
	return out
}

func uniqueSorted(in []int) []int {
	if len(in) == 0 {
		return nil
	}
	out := append([]int(nil), in...)
	sort.Ints(out)
	dst := out[:1]
	for _, v := range out[1:] {
		if v != dst[len(dst)-1] {
			dst = append(dst, v)
		}
	}
	return dst
}
