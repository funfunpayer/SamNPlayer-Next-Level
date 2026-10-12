package generator

import (
	"context"
	"fmt"
	"math"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// ROIVerifyResult is a lightweight second-pass check of a proposed region.
// It never changes the box — only warns when motion energy inside the ROI
// looks weak relative to the frame (classic cross-check after auto-detect).
type ROIVerifyResult struct {
	Score   float64 `json:"score"`
	Warning string  `json:"warning,omitempty"`
}

// VerifyROI samples early grayscale frames and compares mean absolute
// frame-to-frame change inside the ROI versus the full frame. Soft fail only.
func VerifyROI(ctx context.Context, videoPath string, roi ROI) ROIVerifyResult {
	if ctx == nil {
		ctx = context.Background()
	}
	if roi.W < 4 || roi.H < 4 {
		return ROIVerifyResult{Warning: "Region too small — please redraw"}
	}
	info, err := videox.Probe(ctx, videoPath)
	if err != nil {
		return ROIVerifyResult{} // silent: probe failure is not an ROI issue
	}
	r, err := videox.NewGrayReader(ctx, videoPath, info, videox.GrayReaderOptions{
		FPS:      10,
		MaxWidth: 320,
	})
	if err != nil {
		return ROIVerifyResult{}
	}
	defer r.Close()

	vw, vh := info.Width, info.Height
	if info.Rotation == 90 || info.Rotation == 270 {
		vw, vh = info.Height, info.Width
	}
	sx := float64(r.Width) / float64(vw)
	sy := float64(r.Height) / float64(vh)
	rx := int(math.Round(float64(roi.X) * sx))
	ry := int(math.Round(float64(roi.Y) * sy))
	rw := int(math.Round(float64(roi.W) * sx))
	rh := int(math.Round(float64(roi.H) * sy))
	if rw < 2 {
		rw = 2
	}
	if rh < 2 {
		rh = 2
	}
	if rx < 0 {
		rx = 0
	}
	if ry < 0 {
		ry = 0
	}
	if rx+rw > r.Width {
		rx = max(0, r.Width-rw)
	}
	if ry+rh > r.Height {
		ry = max(0, r.Height-rh)
	}

	const maxFrames = 40
	var prev []byte
	var sumIn, sumAll float64
	var pairs int
	for i := 0; i < maxFrames; i++ {
		fr, err := r.Next(i)
		if err != nil {
			break
		}
		if prev != nil {
			inE, allE := frameDiffEnergy(prev, fr.Pixels, r.Width, r.Height, rx, ry, rw, rh)
			sumIn += inE
			sumAll += allE
			pairs++
		}
		prev = fr.Pixels
	}
	if pairs < 4 || sumAll <= 0 {
		return ROIVerifyResult{Score: 0, Warning: "Second pass found little motion — please review the region"}
	}
	roiArea := float64(rw * rh)
	frameArea := float64(r.Width * r.Height)
	if roiArea <= 0 || frameArea <= 0 {
		return ROIVerifyResult{}
	}
	// Expected share of motion if uniform: roiArea/frameArea.
	// Score = actual share / expected share (1 = neutral, >1 concentrated).
	actualShare := sumIn / sumAll
	expected := roiArea / frameArea
	score := actualShare / expected
	out := ROIVerifyResult{Score: score}
	coverage := roiArea / frameArea
	// Soft fallback when Tip-Find still proposes a near-full-frame box:
	// ask the user to drag smaller — never auto-rewrite the box.
	if coverage > 0.40 {
		out.Warning = fmt.Sprintf(
			"Tip box covers %.0f%% of the frame — drag to tighten toward the tip (Tip-Find is editable)",
			coverage*100)
		return out
	}
	if score < 0.55 {
		out.Warning = fmt.Sprintf(
			"Second pass: region looks weak (motion concentration %.2f) — drag to refine the box",
			score)
	}
	return out
}

func frameDiffEnergy(a, b []byte, w, h, rx, ry, rw, rh int) (in, all float64) {
	for y := 0; y < h; y++ {
		row := y * w
		for x := 0; x < w; x++ {
			d := math.Abs(float64(int(a[row+x]) - int(b[row+x])))
			all += d
			if x >= rx && x < rx+rw && y >= ry && y < ry+rh {
				in += d
			}
		}
	}
	return in, all
}
