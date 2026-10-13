package motionx

// Chapter is a user-facing label for a stretch of script motion.
// It is not semantic content detection — only how the curve moves.
type Chapter struct {
	Kind    string  `json:"kind"` // pause | build | steady | crescendo | winddown
	StartMs float64 `json:"startMs"`
	EndMs   float64 `json:"endMs"`
}

// Chapters maps classifier states onto coarser playback chapters.
func Chapters(segs []Segment) []Chapter {
	if len(segs) == 0 {
		return nil
	}
	out := make([]Chapter, 0, len(segs))
	for i, s := range segs {
		kind := "steady"
		switch s.State {
		case Static:
			kind = "pause"
		case Starting, Accelerating:
			kind = "build"
			if i >= len(segs)*3/4 {
				kind = "crescendo"
			}
		case Decelerating, Stopping:
			kind = "winddown"
		case Regular:
			kind = "steady"
		}
		out = append(out, Chapter{Kind: kind, StartMs: s.StartMs, EndMs: s.EndMs})
	}
	return mergeSameKind(out)
}

func mergeSameKind(in []Chapter) []Chapter {
	if len(in) == 0 {
		return in
	}
	out := []Chapter{in[0]}
	for _, c := range in[1:] {
		last := &out[len(out)-1]
		if c.Kind == last.Kind {
			last.EndMs = c.EndMs
			continue
		}
		out = append(out, c)
	}
	return out
}
