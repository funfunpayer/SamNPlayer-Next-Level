package funscript

import (
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
)

// HeatmapPNGOptions controls ExportHeatmapPNG.
type HeatmapPNGOptions struct {
	Width    int
	Height   int
	Chapters []ChapterMark
}

// ExportHeatmapPNG writes an OFS-style intensity heatmap PNG for actions.
// Intensity uses the community formula 500×|Δpos|/|Δt|; chapter starts are
// drawn as thin teal ticks.
func ExportHeatmapPNG(path string, actions []Action, opts HeatmapPNGOptions) error {
	if len(actions) < 2 {
		return fmt.Errorf("funscript: heatmap needs at least 2 actions")
	}
	w, h := opts.Width, opts.Height
	if w <= 0 {
		w = 800
	}
	if h <= 0 {
		h = 48
	}
	dur := actions[len(actions)-1].At - actions[0].At
	if dur <= 0 {
		dur = 1
	}

	buckets := w
	intens := make([]float64, buckets)
	counts := make([]float64, buckets)
	for i := 0; i < len(actions)-1; i++ {
		dt := float64(actions[i+1].At - actions[i].At)
		if dt <= 0 {
			continue
		}
		v := 500.0 * math.Abs(float64(actions[i+1].Pos-actions[i].Pos)) / dt
		mid := actions[i].At + (actions[i+1].At-actions[i].At)/2
		idx := int((mid - actions[0].At) * int64(buckets) / dur)
		if idx < 0 {
			idx = 0
		}
		if idx >= buckets {
			idx = buckets - 1
		}
		intens[idx] += v
		counts[idx]++
	}
	var peak float64
	for i := range intens {
		if counts[i] > 0 {
			intens[i] /= counts[i]
		}
		if intens[i] > peak {
			peak = intens[i]
		}
	}
	if peak <= 0 {
		peak = 1
	}

	img := image.NewRGBA(image.Rect(0, 0, w, h))
	bg := color.RGBA{R: 12, G: 16, B: 24, A: 255}
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.Set(x, y, bg)
		}
	}
	for x := 0; x < w; x++ {
		n := intens[x] / peak
		col := heatmapColor(n)
		barH := int(math.Round(n * float64(h-4)))
		if barH < 1 && intens[x] > 0 {
			barH = 1
		}
		for y := h - 2; y > h-2-barH && y >= 0; y-- {
			img.Set(x, y, col)
		}
	}
	tick := color.RGBA{R: 47, G: 212, B: 196, A: 255}
	for _, ch := range opts.Chapters {
		x := int((ch.StartTime - actions[0].At) * int64(w) / dur)
		if x < 0 || x >= w {
			continue
		}
		for y := 0; y < h; y++ {
			img.Set(x, y, tick)
		}
	}

	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return png.Encode(f, img)
}

func heatmapColor(n float64) color.RGBA {
	if n < 0 {
		n = 0
	}
	if n > 1 {
		n = 1
	}
	// dark → teal → amber → hot
	switch {
	case n < 0.35:
		t := n / 0.35
		return color.RGBA{R: uint8(20 + 27*t), G: uint8(40 + 172*t), B: uint8(60 + 136*t), A: 255}
	case n < 0.7:
		t := (n - 0.35) / 0.35
		return color.RGBA{R: uint8(47 + 196*t), G: uint8(212 - 34*t), B: uint8(196 - 136*t), A: 255}
	default:
		t := (n - 0.7) / 0.3
		return color.RGBA{R: uint8(243), G: uint8(178 - 80*t), B: uint8(60 - 20*t), A: 255}
	}
}
