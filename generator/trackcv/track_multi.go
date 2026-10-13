//go:build cgo && opencv

package trackcv

import "github.com/funfunpayer/SamNPlayer/generator/trackutil"

// Partner is one Tf/Tj contact anchor (ROI2 or extra --target).
type Partner struct {
	ROI   Rect
	Fixed bool
}

// TrackMultiPoints follows the tip ROI plus N contact partners and returns
// min tip→partner distance (TipPartnerDistance) as Positions.
// Lost tracked partners are excluded from min() for that frame (F-006) after
// a short coast budget; tip loss or no usable partner keeps the previous
// distance. Appearance reacquire (when enabled) keeps the same partner slot.
func TrackMultiPoints(videoPath string, tip Rect, partners []Partner, opts Options) (Result, error) {
	if len(partners) == 0 {
		return Result{}, &trackError{"TrackMultiPoints requires at least one partner"}
	}
	if len(partners) == 1 {
		o := opts
		o.FixedB = partners[0].Fixed
		return TrackTwoPoints(videoPath, tip, partners[0].ROI, o)
	}

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

	tipTracker := NewTracker()
	tipTracker.Init(cap, tip)
	boxes := make([]Rect, len(partners))
	trackers := make([]*Tracker, len(partners))
	mems := make([]*appearanceMemory, len(partners))
	coasts := make([]trackutil.Coast, len(partners))
	guards := make([]dispGuard, len(partners))
	include0 := make([]bool, len(partners))
	var tipMem *appearanceMemory
	var tipGuard dispGuard
	if opts.appearanceMemoryEnabled() {
		tipMem = newAppearanceMemory()
	}
	for i, p := range partners {
		boxes[i] = p.ROI
		include0[i] = true
		coasts[i].ObserveOK(p.ROI.X, p.ROI.Y, p.ROI.W, p.ROI.H)
		if !p.Fixed {
			tr := NewTracker()
			tr.Init(cap, p.ROI)
			trackers[i] = tr
			if tipMem != nil {
				mems[i] = newAppearanceMemory()
			}
		}
	}
	if tipMem != nil {
		// Seed tip + partner templates[0] from frame-0 ROIs (TrackROI parity).
		gray0 := cap.ToGray()
		seedAppearanceMemory(tipMem, gray0, tip)
		for i, p := range partners {
			seedAppearanceMemory(mems[i], gray0, p.ROI)
		}
		gray0.Close()
	}
	var tipCoast trackutil.Coast
	tipCoast.ObserveOK(tip.X, tip.Y, tip.W, tip.H)
	defer func() {
		tipTracker.Close()
		if tipMem != nil {
			tipMem.close()
		}
		for i, tr := range trackers {
			if tr != nil {
				tr.Close()
			}
			if mems[i] != nil {
				mems[i].close()
			}
		}
	}()

	tipBox := tip
	d0, _ := FuseTipPartners(tipBox, true, boxes, include0)
	timestamps := []int{0}
	distances := []float64{d0}
	lostFlags := []bool{false}
	lost, valid := 0, 1
	idx := 1
	lastDist := d0

	total := int(cap.Get(CapPropFrameCount))
	total = progressTotalFrames(total, opts.StartTimeSec, fps, opts.MaxFrames)
	prog := newProgressReporter(opts.OnProgress, total)
	prog.report(0)

	include := make([]bool, len(partners))
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
		if tipMem != nil {
			gray = cap.ToGray()
		}
		newTip, okTip := tipTracker.Update(cap)
		okTip, tipBox, tipTracker = recoverOrCoast(cap, gray, tipTracker, tipMem, &tipCoast, &tipGuard, newTip, okTip, tipBox, idx)
		tipOK := okTip
		if !okTip {
			if x, y, w, h, on := tipCoast.OnLost(); on {
				tipBox = Rect{X: x, Y: y, W: w, H: h}
				tipOK = true
			}
		}
		for i := range partners {
			if trackers[i] == nil {
				include[i] = true // fixed
				continue
			}
			newB, okB := trackers[i].Update(cap)
			okB, boxes[i], trackers[i] = recoverOrCoast(cap, gray, trackers[i], mems[i], &coasts[i], &guards[i], newB, okB, boxes[i], idx)
			if okB {
				include[i] = true
			} else if x, y, w, h, on := coasts[i].OnLost(); on {
				boxes[i] = Rect{X: x, Y: y, W: w, H: h}
				include[i] = true
			} else {
				include[i] = false
			}
		}
		dist, fused := FuseTipPartners(tipBox, tipOK, boxes, include)
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
