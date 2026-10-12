package posttrack

import "sort"

// FindPeaks is the exported form of findPeaks (scipy-compatible subset).
// Used by strokepreview and other callers that need peak indices without
// running the full Convert pipeline.
func FindPeaks(y []float64, distance int, prominence float64) []int {
	return findPeaks(y, distance, prominence)
}

// findPeaks mirrors scipy.signal.find_peaks for the subset we use:
// local maxima, optional minimum distance (samples), optional minimum
// prominence. Distance selection prefers taller peaks (scipy order).
func findPeaks(y []float64, distance int, prominence float64) []int {
	n := len(y)
	if n < 3 {
		return nil
	}
	if distance < 1 {
		distance = 1
	}

	candidates := localMaximaWithPlateaus(y)

	if prominence > 0 {
		filtered := candidates[:0]
		for _, i := range candidates {
			if peakProminence(y, i) >= prominence {
				filtered = append(filtered, i)
			}
		}
		candidates = filtered
	}

	if distance <= 1 || len(candidates) == 0 {
		return candidates
	}

	// Sort by height descending, then greedily keep if far enough from
	// already-kept peaks (scipy's distance rule).
	type scored struct {
		idx    int
		height float64
	}
	order := make([]scored, len(candidates))
	for i, idx := range candidates {
		order[i] = scored{idx, y[idx]}
	}
	sort.Slice(order, func(i, j int) bool {
		if order[i].height != order[j].height {
			return order[i].height > order[j].height
		}
		return order[i].idx < order[j].idx
	})

	kept := make([]bool, n)
	out := make([]int, 0, len(order))
	for _, s := range order {
		ok := true
		lo := s.idx - distance + 1
		if lo < 0 {
			lo = 0
		}
		hi := s.idx + distance
		if hi > n {
			hi = n
		}
		for j := lo; j < hi; j++ {
			if kept[j] {
				ok = false
				break
			}
		}
		if ok {
			kept[s.idx] = true
			out = append(out, s.idx)
		}
	}
	sort.Ints(out)
	return out
}

func localMaximaWithPlateaus(y []float64) []int {
	n := len(y)
	out := make([]int, 0)
	i := 1
	for i < n-1 {
		if y[i] < y[i-1] {
			i++
			continue
		}
		// Ascending or flat from left; find end of plateau.
		j := i
		for j+1 < n && y[j+1] == y[i] {
			j++
		}
		leftOK := y[i] > y[i-1]
		rightOK := j+1 < n && y[j] > y[j+1]
		// Also treat edges of equal-to-previous carefully: scipy considers
		// a plateau a peak when both sides are strictly lower.
		if i-1 >= 0 {
			leftOK = y[i] > y[i-1]
		}
		if leftOK && rightOK {
			mid := (i + j) / 2
			out = append(out, mid)
		}
		i = j + 1
	}
	return out
}

// peakProminence is scipy's prominence for a single peak index:
// vertical distance between the peak and its lowest contour line.
func peakProminence(y []float64, peak int) float64 {
	n := len(y)
	if peak <= 0 || peak >= n-1 {
		return 0
	}
	// Left base: walk left until a higher peak, track minimum.
	leftMin := y[peak]
	for i := peak; i >= 0; i-- {
		if y[i] < leftMin {
			leftMin = y[i]
		}
		if y[i] > y[peak] {
			break
		}
	}
	rightMin := y[peak]
	for i := peak; i < n; i++ {
		if y[i] < rightMin {
			rightMin = y[i]
		}
		if y[i] > y[peak] {
			break
		}
	}
	base := leftMin
	if rightMin > leftMin {
		base = rightMin // contour line is the higher of the two minima
	}
	return y[peak] - base
}
