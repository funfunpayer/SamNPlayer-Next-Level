//go:build cgo && opencv

package trackcv

// progressTotalFrames returns the number of frames this tracking run can
// actually process. Container frame counts describe the whole video, so a
// non-zero StartTimeSec must remove the frames skipped by the seek.
func progressTotalFrames(totalFrames int, startTimeSec, fps float64, maxFrames int) int {
	knownTotal := totalFrames > 0
	if knownTotal && startTimeSec > 0 && fps > 0 {
		skipped := int(startTimeSec*fps + 0.5)
		totalFrames -= skipped
		if totalFrames < 0 {
			totalFrames = 0
		}
	}
	if maxFrames > 0 && (!knownTotal || maxFrames < totalFrames) {
		totalFrames = maxFrames
	}
	return totalFrames
}

// progressReporter throttles OnProgress to ~100 updates (same cadence as
// generate_funscript.track_roi PROGRESS lines) so the GUI bar moves during
// long Go-native tracking instead of jumping 0→100 at the end.
type progressReporter struct {
	fn    func(done, total int)
	total int
	next  int
}

func newProgressReporter(fn func(done, total int), totalFrames int) *progressReporter {
	if fn == nil {
		return nil
	}
	if totalFrames < 0 {
		totalFrames = 0
	}
	return &progressReporter{fn: fn, total: totalFrames, next: 0}
}

func (p *progressReporter) report(done int) {
	if p == nil {
		return
	}
	if done < p.next && (p.total <= 0 || done < p.total) {
		return
	}
	p.fn(done, p.total)
	step := 10
	if p.total > 0 {
		step = p.total / 100
		if step < 1 {
			step = 1
		}
	}
	p.next = done + step
}
