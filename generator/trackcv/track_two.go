//go:build cgo && opencv

package trackcv

import "github.com/funfunpayer/SamNPlayer/generator/trackutil"

// TrackTwoPoints follows two ROIs and returns tip→partner distance as Positions —
// Go port of generate_funscript.track_two_points (CSRT; optional appearance
// reacquire when Options.AppearanceMemory; tip/partner coast on brief loss).
// Shared pan cancels in the distance by construction. Distance uses the tip
// box point nearest the partner (TipPartnerDistance), not tip center.
func TrackTwoPoints(videoPath string, roiA, roiB Rect, opts Options) (Result, error) {
	cap := OpenVideo(videoPath)
	defer cap.Close()
	if !cap.IsOpened() {
		return Result{}, &trackError{"Video konnte nicht geöffnet werden: " + videoPath}
	}

	fps := cap.Get(CapPropFPS)
	if fps <= 0 {
		fps = 30.0
	}
	width := int(cap.Get(CapPropFrameWidth))
	height := int(cap.Get(CapPropFrameHeight))

	if opts.StartTimeSec > 0 {
		sf := int(opts.StartTimeSec*fps + 0.5)
		if sf > 0 {
			cap.Seek(sf)
		}
	}

	if !cap.Read() {
		return Result{}, &trackError{"Erster Frame konnte nicht gelesen werden"}
	}

	trackerA := NewTracker()
	trackerA.Init(cap, roiA)
	var trackerB *Tracker
	boxA, boxB := roiA, roiB
	if !opts.FixedB {
		trackerB = NewTracker()
		trackerB.Init(cap, roiB)
	}
	var memA, memB *appearanceMemory
	if opts.appearanceMemoryEnabled() {
		memA = newAppearanceMemory()
		if trackerB != nil {
			memB = newAppearanceMemory()
		}
		// Seed templates[0] from the true frame-0 ROIs (parity with TrackROI).
		gray0 := cap.ToGray()
		seedAppearanceMemory(memA, gray0, roiA)
		seedAppearanceMemory(memB, gray0, roiB)
		gray0.Close()
	}
	defer func() {
		trackerA.Close()
		if trackerB != nil {
			trackerB.Close()
		}
		if memA != nil {
			memA.close()
		}
		if memB != nil {
			memB.close()
		}
	}()

	timestamps := []int{0}
	lastDist := TipPartnerDistance(boxA, boxB)
	distances := []float64{lastDist}
	lostFlags := []bool{false}
	lost, valid := 0, 1
	idx := 1
	var coastA, coastB trackutil.Coast
	var guardA, guardB dispGuard
	coastA.ObserveOK(boxA.X, boxA.Y, boxA.W, boxA.H)
	coastB.ObserveOK(boxB.X, boxB.Y, boxB.W, boxB.H)
	var trajA, trajB []Point
	if opts.CaptureTrajectory {
		trajA = []Point{boxCenter(boxA)}
		trajB = []Point{boxCenter(boxB)}
	}

	total := int(cap.Get(CapPropFrameCount))
	total = progressTotalFrames(total, opts.StartTimeSec, fps, opts.MaxFrames)
	prog := newProgressReporter(opts.OnProgress, total)
	prog.report(0)

	for {
		if opts.Cancel != nil && opts.Cancel() {
			return Result{Canceled: true}, ErrCanceled
		}
		if opts.MaxFrames > 0 && idx >= opts.MaxFrames {
			break
		}
		if !cap.Read() {
			break
		}
		var gray *Gray
		if memA != nil || memB != nil {
			gray = cap.ToGray()
		}
		newA, okA := trackerA.Update(cap)
		okA, boxA, trackerA = recoverOrCoast(cap, gray, trackerA, memA, &coastA, &guardA, newA, okA, boxA, idx)
		tipOK := okA
		if !okA {
			if x, y, w, h, on := coastA.OnLost(); on {
				boxA = Rect{X: x, Y: y, W: w, H: h}
				tipOK = true
			}
		}

		includeB := true
		if trackerB != nil {
			newB, okB := trackerB.Update(cap)
			okB, boxB, trackerB = recoverOrCoast(cap, gray, trackerB, memB, &coastB, &guardB, newB, okB, boxB, idx)
			if okB {
				includeB = true
			} else if x, y, w, h, on := coastB.OnLost(); on {
				boxB = Rect{X: x, Y: y, W: w, H: h}
				includeB = true
			} else {
				includeB = false
			}
		} else {
			// Fixed partner always included.
			includeB = opts.FixedB
		}

		dist, fused := FuseTipPartners(boxA, tipOK, []Rect{boxB}, []bool{includeB})
		frameLost := !fused
		if frameLost {
			lost++
			dist = lastDist
		} else {
			valid++
			lastDist = dist
		}
		if gray != nil {
			gray.Close()
		}
		distances = append(distances, dist)
		timestamps = append(timestamps, int(float64(idx)*1000.0/fps))
		lostFlags = append(lostFlags, frameLost)
		if opts.CaptureTrajectory {
			trajA = append(trajA, boxCenter(boxA))
			trajB = append(trajB, boxCenter(boxB))
		}
		prog.report(idx)
		idx++
	}
	prog.report(idx)

	if opts.StartTimeSec > 0 {
		off := int(opts.StartTimeSec*1000 + 0.5)
		for i := range timestamps {
			timestamps[i] += off
		}
	}

	conf := 0.0
	if idx > 0 {
		conf = float64(valid) / float64(idx)
	}
	reason := ""
	if idx > 0 && float64(lost)/float64(idx) > 0.5 {
		reason = "tracker_lost_heavy"
	} else if idx > 0 && float64(lost)/float64(idx) > 0.15 {
		reason = "tracker_lost_elevated"
	}

	return Result{
		TimestampsMs: timestamps,
		Positions:    distances,
		Width:        width,
		Height:       height,
		LostFlags:    lostFlags,
		TrajectoryA:  trajA,
		TrajectoryB:  trajB,
		Stats: Stats{
			TrackerLostFrames: lost,
			TotalFrames:       idx,
			VerticalRange:     ptp(distances),
			ValidFrames:       valid,
			Confidence:        conf,
			Reason:            reason,
		},
	}, nil
}
