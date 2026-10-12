package posttrack

import "math"

// percentile returns the linear-interpolation percentile (numpy default).
func percentile(values []float64, p float64) float64 {
	n := len(values)
	if n == 0 {
		return 0
	}
	if n == 1 {
		return values[0]
	}
	sorted := append([]float64(nil), values...)
	// insertion sort is fine for small windows; for full curves use sort.
	sortFloat64s(sorted)
	if p <= 0 {
		return sorted[0]
	}
	if p >= 100 {
		return sorted[n-1]
	}
	rank := (p / 100.0) * float64(n-1)
	lo := int(math.Floor(rank))
	hi := int(math.Ceil(rank))
	if lo == hi {
		return sorted[lo]
	}
	w := rank - float64(lo)
	return sorted[lo]*(1-w) + sorted[hi]*w
}

func sortFloat64s(a []float64) {
	// Simple heap-free sort via Go's sort package wrapper in convert; here
	// keep a tiny insertion sort to avoid an import cycle in tests of helpers.
	for i := 1; i < len(a); i++ {
		v := a[i]
		j := i
		for j > 0 && a[j-1] > v {
			a[j] = a[j-1]
			j--
		}
		a[j] = v
	}
}

func minMax(values []float64) (lo, hi float64) {
	if len(values) == 0 {
		return 0, 0
	}
	lo, hi = values[0], values[0]
	for _, v := range values[1:] {
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return lo, hi
}

func ptp(values []float64) float64 {
	lo, hi := minMax(values)
	return hi - lo
}

func medianDiff(times []float64) float64 {
	if len(times) < 2 {
		return 1
	}
	diffs := make([]float64, len(times)-1)
	for i := 1; i < len(times); i++ {
		diffs[i-1] = times[i] - times[i-1]
	}
	sortFloat64s(diffs)
	mid := len(diffs) / 2
	if len(diffs)%2 == 0 {
		return (diffs[mid-1] + diffs[mid]) / 2
	}
	return diffs[mid]
}

func clip01(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

// dynamicRangeNormalize lifts weak sections toward usable amplitude.
// Port of generate_funscript.dynamic_range_normalize.
func dynamicRangeNormalize(values []float64, window int, maxGain, minLocalSpan float64) []float64 {
	n := len(values)
	if window < 8 || n < window*2 {
		out := make([]float64, n)
		copy(out, values)
		return out
	}
	if maxGain <= 0 {
		maxGain = 5.0
	}
	if minLocalSpan <= 0 {
		minLocalSpan = 0.12
	}

	globalSpan := ptp(values)
	if globalSpan < 1e-9 {
		out := make([]float64, n)
		copy(out, values)
		return out
	}

	half := window / 2
	padded := padEdge(values, half)
	centers := make([]float64, n)
	spans := make([]float64, n)
	for i := 0; i < n; i++ {
		seg := padded[i : i+window]
		lo := percentile(seg, 5)
		hi := percentile(seg, 95)
		centers[i] = (lo + hi) / 2.0
		spans[i] = hi - lo
	}

	smooth := window / 2
	if smooth < 3 {
		smooth = 3
	}
	centers = movingAverageEdge(centers, smooth)
	spans = movingAverageEdge(spans, smooth)

	out := make([]float64, n)
	for i := 0; i < n; i++ {
		gain := 1.0
		if spans[i] > globalSpan*minLocalSpan {
			gain = globalSpan / math.Max(spans[i], 1e-9)
			if gain < 1 {
				gain = 1
			}
			if gain > maxGain {
				gain = maxGain
			}
		}
		out[i] = centers[i] + (values[i]-centers[i])*gain
	}
	return out
}

func padEdge(values []float64, pad int) []float64 {
	n := len(values)
	out := make([]float64, n+2*pad)
	for i := 0; i < pad; i++ {
		out[i] = values[0]
		out[n+pad+i] = values[n-1]
	}
	copy(out[pad:], values)
	return out
}
