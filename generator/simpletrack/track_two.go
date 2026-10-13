package simpletrack

import (
	"context"
	"fmt"

	"github.com/funfunpayer/SamNPlayer/generator/trackutil"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// TrackTwoPoints follows two ROIs and returns tip→partner distance as
// the position signal — Go port of generate_funscript.track_two_points.
// Shared pan cancels in the distance; lost flags go into Stats for
// tracking_gaps (contact vibration mute). Distance uses tip box point
// nearest the partner (tipPartnerDistance), not tip center.
func TrackTwoPoints(ctx context.Context, videoPath string, roiA, roiB Rect, opts Options) (Result, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if err := ctx.Err(); err != nil {
		return Result{}, err
	}
	if roiA.W < 4 || roiA.H < 4 || roiB.W < 4 || roiB.H < 4 {
		return Result{}, fmt.Errorf("simpletrack: ROI too small for two-point")
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
	srcW, srcH := info.Rotated()
	if srcW > 0 && srcH > 0 && (srcW != w || srcH != h) {
		sx := float64(w) / float64(srcW)
		sy := float64(h) / float64(srcH)
		roiA = scaleRect(roiA, sx, sy)
		roiB = scaleRect(roiB, sx, sy)
	}
	boxA := clampRect(roiA, w, h)
	boxB := clampRect(roiB, w, h)
	tmplA := extract(frame.Pixels, w, h, boxA)
	if len(tmplA) == 0 {
		return Result{}, fmt.Errorf("simpletrack: empty two-point template")
	}
	var tmplB []byte
	if !opts.FixedB {
		tmplB = extract(frame.Pixels, w, h, boxB)
		if len(tmplB) == 0 {
			return Result{}, fmt.Errorf("simpletrack: empty two-point template")
		}
	}

	timestamps := []int{0}
	lastDist := tipPartnerDistance(boxA, boxB)
	distances := []float64{lastDist}
	lostFlags := []bool{false}
	lost, valid := 0, 1
	frameIdx := 1
	var coastA, coastB trackutil.Coast
	coastA.ObserveOK(boxA.X, boxA.Y, boxA.W, boxA.H)
	coastB.ObserveOK(boxB.X, boxB.Y, boxB.W, boxB.H)
	var trajA, trajB []Point
	if opts.CaptureTrajectory {
		trajA = []Point{boxCenter(boxA)}
		trajB = []Point{boxCenter(boxB)}
	}

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
		bestA, scoreA, okA := searchNCC(frame.Pixels, w, h, tmplA, boxA, margin, step)
		okA = okA && scoreA >= 0.35
		if okA {
			boxA = bestA
			coastA.ObserveOK(boxA.X, boxA.Y, boxA.W, boxA.H)
			if frameIdx%15 == 0 {
				tmplA = extract(frame.Pixels, w, h, boxA)
			}
		} else if x, y, ww, hh, on := coastA.OnLost(); on {
			boxA = Rect{X: x, Y: y, W: ww, H: hh}
			okA = true // coasted tip still usable for fusion
		}
		okB := true
		includeB := true
		if !opts.FixedB {
			bestB, scoreB, foundB := searchNCC(frame.Pixels, w, h, tmplB, boxB, margin, step)
			okB = foundB && scoreB >= 0.35
			if okB {
				boxB = bestB
				coastB.ObserveOK(boxB.X, boxB.Y, boxB.W, boxB.H)
				if frameIdx%15 == 0 {
					tmplB = extract(frame.Pixels, w, h, boxB)
				}
				includeB = true
			} else if x, y, ww, hh, on := coastB.OnLost(); on {
				boxB = Rect{X: x, Y: y, W: ww, H: hh}
				includeB = true
			} else {
				includeB = false
			}
		}
		dist, fused := fuseTipPartners(boxA, okA, []Rect{boxB}, []bool{includeB})
		frameLost := !fused
		if frameLost {
			lost++
			dist = lastDist
		} else {
			valid++
			lastDist = dist
		}
		distances = append(distances, dist)
		timestamps = append(timestamps, int(float64(frameIdx)*1000.0/fps))
		lostFlags = append(lostFlags, frameLost)
		if opts.CaptureTrajectory {
			trajA = append(trajA, boxCenter(boxA))
			trajB = append(trajB, boxCenter(boxB))
		}
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

	return Result{
		TimestampsMs: timestamps,
		Positions:    distances,
		Width:        w,
		Height:       h,
		LostFlags:    lostFlags,
		TrajectoryA:  trajA,
		TrajectoryB:  trajB,
		Stats: Stats{
			TrackerLostFrames: lost,
			TotalFrames:       frameIdx,
			VerticalRange:     ptp(distances),
			ValidFrames:       valid,
			Confidence:        conf,
			Reason:            reason,
		},
	}, nil
}

func scaleRect(r Rect, sx, sy float64) Rect {
	out := Rect{
		X: int(float64(r.X) * sx),
		Y: int(float64(r.Y) * sy),
		W: int(float64(r.W) * sx),
		H: int(float64(r.H) * sy),
	}
	if out.W < 4 {
		out.W = 4
	}
	if out.H < 4 {
		out.H = 4
	}
	return out
}
