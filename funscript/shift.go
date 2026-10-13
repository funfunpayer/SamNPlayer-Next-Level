package funscript

// ShiftActions moves every action by deltaMs (positive = later). Actions
// that would land before 0 ms are dropped and counted, so the caller can
// say so — nothing is clipped silently. Input is not modified.
func ShiftActions(actions []Action, deltaMs int64) (out []Action, dropped int) {
	out = make([]Action, 0, len(actions))
	for _, a := range actions {
		at := a.At + deltaMs
		if at < 0 {
			dropped++
			continue
		}
		a.At = at
		out = append(out, a)
	}
	return out, dropped
}

// ShiftBookmarks moves bookmarks by deltaMs; ones before 0 are dropped.
func ShiftBookmarks(bms []Bookmark, deltaMs int64) (out []Bookmark, dropped int) {
	out = make([]Bookmark, 0, len(bms))
	for _, b := range bms {
		if b.Time+deltaMs < 0 {
			dropped++
			continue
		}
		b.Time += deltaMs
		out = append(out, b)
	}
	return out, dropped
}

// ShiftChapters moves chapters by deltaMs. A chapter that starts before 0
// is clamped to 0 when its end is still after 0, otherwise dropped.
func ShiftChapters(chs []ChapterMark, deltaMs int64) (out []ChapterMark, dropped int) {
	out = make([]ChapterMark, 0, len(chs))
	for _, c := range chs {
		start := c.StartTime + deltaMs
		end := c.EndTime
		if end > 0 {
			end += deltaMs
		}
		if start < 0 {
			if c.EndTime > 0 && end > 0 {
				start = 0
			} else {
				dropped++
				continue
			}
		}
		c.StartTime, c.EndTime = start, end
		out = append(out, c)
	}
	return out, dropped
}
