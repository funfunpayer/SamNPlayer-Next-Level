package posttrack

// savgol is a Savitzky-Golay smoother matching
// scipy.signal.savgol_filter(y, window, polyorder, mode="interp"):
// at each point a least-squares polynomial of degree polyorder is fit to a
// window of `window` samples and evaluated there. At the edges the window
// stays full-sized and is shifted (not shrunk or mirrored) — scipy's default
// "interp" behaviour, not the more common mirror-at-edge mode.
//
// Same algorithm as generator/trackcv's unexported savgol (kept separate so
// this pure-Go package has no cgo dependency on trackcv).
func savgol(y []float64, window, polyorder int) []float64 {
	n := len(y)
	if n < window || window < 1 {
		out := make([]float64, n)
		copy(out, y)
		return out
	}

	out := make([]float64, n)
	for i := 0; i < n; i++ {
		start := i - window/2
		if start < 0 {
			start = 0
		}
		if start > n-window {
			start = n - window
		}
		coeffs := polyfit(y[start:start+window], polyorder)
		x := float64(i - start)
		out[i] = evalPoly(coeffs, x)
	}
	return out
}

func polyfit(y []float64, order int) []float64 {
	n := len(y)
	terms := order + 1
	ata := make([][]float64, terms)
	aty := make([]float64, terms)
	for i := range ata {
		ata[i] = make([]float64, terms)
	}
	for xi := 0; xi < n; xi++ {
		x := float64(xi)
		pow := make([]float64, terms)
		p := 1.0
		for k := 0; k < terms; k++ {
			pow[k] = p
			p *= x
		}
		for r := 0; r < terms; r++ {
			aty[r] += pow[r] * y[xi]
			for c := 0; c < terms; c++ {
				ata[r][c] += pow[r] * pow[c]
			}
		}
	}
	return solveLinear(ata, aty)
}

func evalPoly(coeffs []float64, x float64) float64 {
	result := 0.0
	p := 1.0
	for _, c := range coeffs {
		result += c * p
		p *= x
	}
	return result
}

func solveLinear(a [][]float64, b []float64) []float64 {
	n := len(b)
	m := make([][]float64, n)
	for i := range a {
		m[i] = append([]float64(nil), a[i]...)
	}
	rhs := append([]float64(nil), b...)

	for col := 0; col < n; col++ {
		pivot := col
		for r := col + 1; r < n; r++ {
			if abs(m[r][col]) > abs(m[pivot][col]) {
				pivot = r
			}
		}
		m[col], m[pivot] = m[pivot], m[col]
		rhs[col], rhs[pivot] = rhs[pivot], rhs[col]

		if abs(m[col][col]) < 1e-12 {
			continue
		}
		for r := col + 1; r < n; r++ {
			factor := m[r][col] / m[col][col]
			for c := col; c < n; c++ {
				m[r][c] -= factor * m[col][c]
			}
			rhs[r] -= factor * rhs[col]
		}
	}

	x := make([]float64, n)
	for i := n - 1; i >= 0; i-- {
		sum := rhs[i]
		for j := i + 1; j < n; j++ {
			sum -= m[i][j] * x[j]
		}
		if abs(m[i][i]) < 1e-12 {
			x[i] = 0
			continue
		}
		x[i] = sum / m[i][i]
	}
	return x
}

func abs(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}
