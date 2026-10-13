package posttrack

import "math"

// RollingDetrend subtracts a centered moving average (high-pass drift removal).
// Funscript-Flow / FunGen-style: kill slow camera/lighting wander before normalize.
func RollingDetrend(values []float64, windowMs, sampleStepMs float64) []float64 {
	n := len(values)
	if n == 0 || windowMs <= 0 || sampleStepMs <= 0 {
		return append([]float64(nil), values...)
	}
	win := int(math.Round(windowMs / sampleStepMs))
	if win < 3 {
		win = 3
	}
	if win%2 == 0 {
		win++
	}
	if win > n {
		win = n
		if win%2 == 0 {
			win--
		}
		if win < 3 {
			return append([]float64(nil), values...)
		}
	}
	half := win / 2
	out := make([]float64, n)
	pref := make([]float64, n+1)
	for i, v := range values {
		pref[i+1] = pref[i] + v
	}
	for i := 0; i < n; i++ {
		lo := i - half
		hi := i + half
		if lo < 0 {
			lo = 0
		}
		if hi >= n {
			hi = n - 1
		}
		count := float64(hi - lo + 1)
		mean := (pref[hi+1] - pref[lo]) / count
		out[i] = values[i] - mean
	}
	return out
}

// Bandpass keeps roughly [lowHz, highHz] with cascaded one-pole filters,
// applied forward+backward (zero-phase, filtfilt-style). O(n), no FFT.
// Device-useful stroke band is typically ~0.5–4 Hz.
func Bandpass(values []float64, sampleHz, lowHz, highHz float64) []float64 {
	n := len(values)
	if n < 8 || sampleHz <= 0 {
		return append([]float64(nil), values...)
	}
	if lowHz <= 0 && highHz <= 0 {
		return append([]float64(nil), values...)
	}
	if highHz > 0 && lowHz > highHz {
		lowHz, highHz = highHz, lowHz
	}
	out := append([]float64(nil), values...)
	if lowHz > 0 && lowHz < sampleHz/2 {
		out = filtfilt1pole(out, onePoleHighpassAlpha(lowHz, sampleHz), true)
	}
	if highHz > 0 && highHz < sampleHz/2 {
		out = filtfilt1pole(out, onePoleLowpassAlpha(highHz, sampleHz), false)
	}
	return out
}

func onePoleLowpassAlpha(cutoffHz, sampleHz float64) float64 {
	// alpha = dt / (RC + dt), RC = 1/(2πf)
	dt := 1.0 / sampleHz
	rc := 1.0 / (2 * math.Pi * cutoffHz)
	return dt / (rc + dt)
}

func onePoleHighpassAlpha(cutoffHz, sampleHz float64) float64 {
	// Same RC mapping as lowpass; used with high-pass recurrence.
	dt := 1.0 / sampleHz
	rc := 1.0 / (2 * math.Pi * cutoffHz)
	return rc / (rc + dt)
}

// filtfilt1pole runs a one-pole LPF or HPF forward then backward.
func filtfilt1pole(x []float64, alpha float64, highpass bool) []float64 {
	if alpha <= 0 || alpha >= 1 || len(x) == 0 {
		return append([]float64(nil), x...)
	}
	fwd := onePole(x, alpha, highpass)
	// reverse
	rev := make([]float64, len(fwd))
	for i := range fwd {
		rev[i] = fwd[len(fwd)-1-i]
	}
	bwd := onePole(rev, alpha, highpass)
	out := make([]float64, len(bwd))
	for i := range bwd {
		out[i] = bwd[len(bwd)-1-i]
	}
	return out
}

func onePole(x []float64, alpha float64, highpass bool) []float64 {
	y := make([]float64, len(x))
	if len(x) == 0 {
		return y
	}
	if highpass {
		y[0] = x[0]
		for i := 1; i < len(x); i++ {
			y[i] = alpha * (y[i-1] + x[i] - x[i-1])
		}
	} else {
		y[0] = x[0]
		for i := 1; i < len(x); i++ {
			y[i] = y[i-1] + alpha*(x[i]-y[i-1])
		}
	}
	return y
}
