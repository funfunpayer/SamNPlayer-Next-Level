package posttrack

// movingAverageEdge matches:
//
//	np.convolve(np.pad(x, k, mode="edge"), np.ones(k)/k, "same")[k:-k]
func movingAverageEdge(values []float64, k int) []float64 {
	n := len(values)
	if k < 1 || n == 0 {
		out := make([]float64, n)
		copy(out, values)
		return out
	}
	padded := padEdge(values, k)
	// Full convolution length = len(padded)+k-1; 'same' takes the centre
	// slice of length len(padded); then trim the outer pad of size k.
	fullLen := len(padded) + k - 1
	full := make([]float64, fullLen)
	invK := 1.0 / float64(k)
	for i := 0; i < len(padded); i++ {
		v := padded[i] * invK
		for j := 0; j < k; j++ {
			full[i+j] += v
		}
	}
	start := (k - 1) / 2
	same := full[start : start+len(padded)]
	out := make([]float64, n)
	copy(out, same[k:k+n])
	return out
}
