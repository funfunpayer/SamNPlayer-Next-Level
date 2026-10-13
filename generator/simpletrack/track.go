// Package simpletrack is a pure-Go ROI tracker over videox (ffmpeg).
//
// Weaker than CSRT (OpenCV), but runs without cgo/OpenCV — the Windows
// release path that cannot link OpenCV. Uses normalized cross-correlation
// template matching in a search window around the last box.
package simpletrack

import (
	"context"
	"errors"
	"fmt"
	"math"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// Rect is an axis-aligned ROI in pixel coordinates.
type Rect struct{ X, Y, W, H int }

// Options controls TrackROI.
type Options struct {
	MaxFrames    int // 0 = unlimited
	StartTimeSec float64
	Axis         string
	Cancel       func() bool
	SearchMargin int // pixels around last box; 0 → 32
	Step         int // search stride; 0 → 2
	// FixedB keeps ROI B at the initial box (static Tf/Tj contact target).
	FixedB bool
	// OnProgress is called ~every 1% of frames (done, total). total may be 0
	// when duration is unknown.
	OnProgress func(done, total int)
	// CaptureTrajectory opts into keeping raw per-frame tip/partner center
	// points on Result.TrajectoryA/B (MT-Debug overlay data, mirrors
	// trackcv.Options.CaptureTrajectory). Off by default: zero extra
	// allocation/behavior when false.
	CaptureTrajectory bool
}

// Stats mirrors trackcv observation fields for native metadata.
type Stats struct {
	TrackerLostFrames int
	TotalFrames       int
	VerticalRange     float64
	HorizontalRange   float64
	ValidFrames       int
	Confidence        float64
	Reason            string
}

// Result is the tracked curve.
type Result struct {
	TimestampsMs []int
	Positions    []float64
	Width        int
	Height       int
	Stats        Stats
	Canceled     bool
	// LostFlags is set by TrackTwoPoints: true when at least one of the two
	// trackers failed that frame (for tracking_gaps / contact mute).
	LostFlags []bool
	// TrajectoryA/B: per-frame tip/partner center points (same pixel space
	// as Width/Height), same length/order as TimestampsMs - only populated
	// when Options.CaptureTrajectory is true. TrackROI only ever fills
	// TrajectoryA (single ROI, no partner).
	TrajectoryA []Point
	TrajectoryB []Point
}

// Point is a video-pixel-space (x,y) sample - MT-Debug trajectory capture.
type Point struct{ X, Y float64 }

func boxCenter(r Rect) Point {
	return Point{X: float64(r.X) + float64(r.W)/2, Y: float64(r.Y) + float64(r.H)/2}
}

// ErrCanceled is returned when Options.Cancel aborts.
var ErrCanceled = errors.New("simpletrack: tracking canceled")

// TrackROI follows roi through videoPath using NCC template matching.
func TrackROI(ctx context.Context, videoPath string, roi Rect, opts Options) (Result, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if err := ctx.Err(); err != nil {
		return Result{}, err
	}
	if roi.W < 4 || roi.H < 4 {
		return Result{}, fmt.Errorf("simpletrack: ROI too small")
	}
	margin := opts.SearchMargin
	if margin <= 0 {
		margin = 32
	}
	step := opts.Step
	if step <= 0 {
		step = 2
	}

	info, err := videox.Probe(ctx, videoPath)
	if err != nil {
		return Result{}, fmt.Errorf("simpletrack: probe: %w", err)
	}
	fps := info.FPS
	if fps <= 0 {
		fps = 25
	}

	r, err := videox.NewGrayReader(ctx, videoPath, info, videox.GrayReaderOptions{
		FPS:        fps,
		MaxWidth:   640,
		AutoRotate: true,
		StartSec:   opts.StartTimeSec,
	})
	if err != nil {
		return Result{}, fmt.Errorf("simpletrack: open: %w", err)
	}
	defer r.Close()

	frame, err := r.Next(0)
	if err != nil {
		return Result{}, fmt.Errorf("simpletrack: first frame: %w", err)
	}
	w, h := frame.Width, frame.Height
	// ROI is in source (rotated) coordinates; scale to the gray reader's geometry.
	srcW, srcH := info.Rotated()
	if srcW > 0 && srcH > 0 && (srcW != w || srcH != h) {
		roi = Rect{
			X: int(float64(roi.X) * float64(w) / float64(srcW)),
			Y: int(float64(roi.Y) * float64(h) / float64(srcH)),
			W: int(float64(roi.W) * float64(w) / float64(srcW)),
			H: int(float64(roi.H) * float64(h) / float64(srcH)),
		}
		if roi.W < 4 {
			roi.W = 4
		}
		if roi.H < 4 {
			roi.H = 4
		}
	}
	box := clampRect(roi, w, h)
	tmpl := extract(frame.Pixels, w, h, box)
	if len(tmpl) == 0 {
		return Result{}, fmt.Errorf("simpletrack: empty template")
	}

	timestamps := []int{0}
	ys := []float64{float64(box.Y) + float64(box.H)/2}
	xs := []float64{float64(box.X) + float64(box.W)/2}
	lost, valid := 0, 1
	frameIdx := 1

	totalFrames := 0
	if info.Duration > 0 && fps > 0 {
		totalFrames = int(info.Duration.Seconds()*fps + 0.5)
	}
	totalFrames = progressTotalFrames(totalFrames, opts.StartTimeSec, fps, opts.MaxFrames)
	prog := newProgressReporter(opts.OnProgress, totalFrames)
	prog.report(0)

	for {
		if opts.Cancel != nil && opts.Cancel() {
			return Result{Canceled: true}, ErrCanceled
		}
		if err := ctx.Err(); err != nil {
			return Result{Canceled: true}, err
		}
		frame, err = r.Next(frameIdx)
		if err != nil {
			break
		}
		best, score, ok := searchNCC(frame.Pixels, w, h, tmpl, box, margin, step)
		if !ok || score < 0.35 {
			lost++
			ys = append(ys, ys[len(ys)-1])
			xs = append(xs, xs[len(xs)-1])
		} else {
			valid++
			box = best
			ys = append(ys, float64(box.Y)+float64(box.H)/2)
			xs = append(xs, float64(box.X)+float64(box.W)/2)
			// Refresh template slowly so appearance drift doesn't kill the match.
			if frameIdx%15 == 0 {
				tmpl = extract(frame.Pixels, w, h, box)
			}
		}
		timestamps = append(timestamps, int(float64(frameIdx)*1000.0/fps))
		prog.report(frameIdx)
		frameIdx++
		if opts.MaxFrames > 0 && frameIdx >= opts.MaxFrames {
			break
		}
	}
	prog.report(frameIdx)

	if opts.StartTimeSec > 0 {
		off := int(opts.StartTimeSec*1000 + 0.5)
		for i := range timestamps {
			timestamps[i] += off
		}
	}

	vRange, hRange := ptp(ys), ptp(xs)
	positions := ys
	switch opts.Axis {
	case "x":
		positions = xs
	case "y":
		positions = ys
	default:
		if hRange > vRange*1.5 && hRange > 5 {
			positions = xs
		}
	}
	conf := 0.0
	if frameIdx > 0 {
		conf = float64(valid) / float64(frameIdx)
	}
	reason := ""
	if frameIdx > 0 && float64(lost)/float64(frameIdx) > 0.5 {
		reason = "tracker_lost_heavy"
	} else if frameIdx > 0 && float64(lost)/float64(frameIdx) > 0.15 {
		reason = "tracker_lost_elevated"
	}

	var trajA []Point
	if opts.CaptureTrajectory {
		// xs/ys already track the box center each frame - just zip them.
		trajA = make([]Point, len(xs))
		for i := range xs {
			trajA[i] = Point{X: xs[i], Y: ys[i]}
		}
	}

	return Result{
		TimestampsMs: timestamps,
		Positions:    positions,
		Width:        w,
		Height:       h,
		TrajectoryA:  trajA,
		Stats: Stats{
			TrackerLostFrames: lost,
			TotalFrames:       frameIdx,
			VerticalRange:     vRange,
			HorizontalRange:   hRange,
			ValidFrames:       valid,
			Confidence:        conf,
			Reason:            reason,
		},
	}, nil
}

func clampRect(r Rect, w, h int) Rect {
	if r.X < 0 {
		r.X = 0
	}
	if r.Y < 0 {
		r.Y = 0
	}
	if r.X+r.W > w {
		r.W = w - r.X
	}
	if r.Y+r.H > h {
		r.H = h - r.Y
	}
	if r.W < 1 {
		r.W = 1
	}
	if r.H < 1 {
		r.H = 1
	}
	return r
}

func extract(pix []byte, w, h int, r Rect) []byte {
	r = clampRect(r, w, h)
	out := make([]byte, r.W*r.H)
	for y := 0; y < r.H; y++ {
		copy(out[y*r.W:(y+1)*r.W], pix[(r.Y+y)*w+r.X:(r.Y+y)*w+r.X+r.W])
	}
	return out
}

func searchNCC(pix []byte, w, h int, tmpl []byte, box Rect, margin, step int) (Rect, float64, bool) {
	tw, th := box.W, box.H
	if tw*th != len(tmpl) || tw < 1 || th < 1 {
		return box, 0, false
	}
	x0 := max(0, box.X-margin)
	y0 := max(0, box.Y-margin)
	x1 := min(w-tw, box.X+margin)
	y1 := min(h-th, box.Y+margin)
	if x1 < x0 || y1 < y0 {
		return box, 0, false
	}

	tMean, tNorm := meanNorm(tmpl)
	useSAD := tNorm < 1e-6

	bestScore := -1.0
	best := box
	patch := make([]byte, tw*th)
	for y := y0; y <= y1; y += step {
		for x := x0; x <= x1; x += step {
			for row := 0; row < th; row++ {
				copy(patch[row*tw:(row+1)*tw], pix[(y+row)*w+x:(y+row)*w+x+tw])
			}
			var score float64
			if useSAD {
				var sad float64
				for i := range tmpl {
					d := float64(tmpl[i]) - float64(patch[i])
					if d < 0 {
						d = -d
					}
					sad += d
				}
				score = 1.0 - sad/(255.0*float64(len(tmpl)))
			} else {
				pMean, pNorm := meanNorm(patch)
				if pNorm < 1e-6 {
					continue
				}
				var sum float64
				for i := range tmpl {
					sum += (float64(tmpl[i]) - tMean) * (float64(patch[i]) - pMean)
				}
				score = sum / (tNorm * pNorm)
			}
			if score > bestScore {
				bestScore = score
				best = Rect{X: x, Y: y, W: tw, H: th}
			}
		}
	}
	return best, bestScore, bestScore >= 0
}

func meanNorm(v []byte) (mean, norm float64) {
	if len(v) == 0 {
		return 0, 0
	}
	var sum float64
	for _, b := range v {
		sum += float64(b)
	}
	mean = sum / float64(len(v))
	var ss float64
	for _, b := range v {
		d := float64(b) - mean
		ss += d * d
	}
	return mean, math.Sqrt(ss)
}

func ptp(v []float64) float64 {
	if len(v) == 0 {
		return 0
	}
	lo, hi := v[0], v[0]
	for _, x := range v {
		if x < lo {
			lo = x
		}
		if x > hi {
			hi = x
		}
	}
	return hi - lo
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
