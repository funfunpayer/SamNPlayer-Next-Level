//go:build cgo && opencv

package trackcv

import "math"

// DefaultScanWindows is the plan default for ScanSceneMap (6 × 8s).
const DefaultScanWindows = 6

// ScanSceneMap runs a quick rhythm heatmap over a video without CSRT.
// It samples n windows of ~8s spaced across the clip (skipping near ends),
// computes FlowCells + rhythmScores only, and returns a SceneMap with no
// ChosenCell / box / sign (those need a full TrackROI run).
//
// Cost class: ~12–20 s for a 280 s clip at the #233 Farneback budget.
// Trigger is explicit (Owner decision 24 Sep): Advanced "Show scene map".
func ScanSceneMap(videoPath string, n int) (SceneMap, error) {
	if n <= 0 {
		n = DefaultScanWindows
	}
	cap := OpenVideo(videoPath)
	defer cap.Close()
	if !cap.IsOpened() {
		return SceneMap{}, &trackError{"Video konnte nicht geöffnet werden: " + videoPath}
	}
	fps := cap.Get(CapPropFPS)
	if fps <= 0 {
		fps = 30.0
	}
	width := int(cap.Get(CapPropFrameWidth))
	height := int(cap.Get(CapPropFrameHeight))
	totalFrames := int(cap.Get(CapPropFrameCount))
	if totalFrames <= 0 {
		// Unknown length: fall back to a short probe of the first windows.
		totalFrames = int(math.Round(float64(n) * rhythmWindowSec * fps * 1.5))
	}
	gh := rhythmGridRows(width, height)
	gw := rhythmGridCols
	win := int(math.Round(rhythmWindowSec * fps))
	if win < 8 {
		return SceneMap{Version: 1, Cols: gw, Rows: gh, Width: width, Height: height}, nil
	}

	// Evenly spaced window starts, inset by half a window from each end.
	usable := totalFrames - win
	if usable < 1 {
		usable = 1
	}
	starts := make([]int, 0, n)
	for i := 0; i < n; i++ {
		var s int
		if n == 1 {
			s = usable / 2
		} else {
			s = int(float64(i) * float64(usable) / float64(n-1))
		}
		if s < 0 {
			s = 0
		}
		if s+win > totalFrames {
			s = max(0, totalFrames-win)
		}
		starts = append(starts, s)
	}

	// Collect per-window flow into a dense cellV covering only the sampled
	// frames, then score. We keep a local index map so scoreWindows sees a
	// contiguous slice per window.
	type sample struct {
		startFrame int
		cellV      [][]float32 // win frames, each gw*gh
	}
	samples := make([]sample, 0, n)
	for _, s := range starts {
		cap.Seek(s)
		if !cap.Read() {
			continue
		}
		prev := cap.ToGray()
		cellV := make([][]float32, win)
		cellV[0] = make([]float32, gw*gh) // first frame has no prior flow
		ok := true
		for i := 1; i < win; i++ {
			if !cap.Read() {
				ok = false
				break
			}
			gray := cap.ToGray()
			_, vy := FlowCells(prev, gray, gw, gh)
			// Axis-agnostic quick scan: use vertical flow (stroke is usually Y).
			// Full runs pick axis after the fact; for a preview heatmap Y is fine.
			cellV[i] = vy
			prev.Close()
			prev = gray
		}
		prev.Close()
		if !ok {
			continue
		}
		samples = append(samples, sample{startFrame: s, cellV: cellV})
	}

	out := SceneMap{
		Version: 1,
		Cols:    gw,
		Rows:    gh,
		Width:   width,
		Height:  height,
	}
	for _, smp := range samples {
		partial := scoreWindows(smp.cellV, gw, gh, width, height, nil, nil, fps)
		for _, w := range partial.Windows {
			// Rebase timestamps to absolute video time.
			offMs := int64(float64(smp.startFrame) * 1000.0 / fps)
			w.StartMs += offMs
			w.EndMs += offMs
			w.scoreRaw = nil
			w.hasScore = false
			out.Windows = append(out.Windows, w)
		}
	}
	return out, nil
}
