package trackcv

// Rect is a pixel-space axis-aligned box (x, y, w, h).
type Rect struct{ X, Y, W, H int }

// ContactPoint is where a teacher (NudeNet, a VLM, later our own detector)
// saw the stroke contact at time Ms (video time), in normalized frame
// coordinates 0..1. See Options.ContactPoints / docs/VLM_TEACHER_PLAN.md.
type ContactPoint struct {
	Ms   int64
	X, Y float64
}

// MarkSample is one Follow-path sample (pixel box at time Ms).
type MarkSample struct {
	Ms   int64
	Rect Rect
}

// SceneMark is a user/auto annotation that can filter rhythm-grid candidates
// (docs/SCENE_MAP_PLAN.md M3). Kind is "exclude", "source", or "region".
// FromMs/ToMs both 0 means the whole clip; otherwise active when
// FromMs <= t <= ToMs.
//
// Follow=true means TrackROI should CSRT-track the painted box so exclude /
// source punch-outs move with the subject (Owner: marks must not stay fixed
// while the body moves). Path is filled by TrackROI when Follow runs; when
// Path is non-empty, sceneMarkRectAt uses the nearest sample.
type SceneMark struct {
	Kind   string // exclude | source | region
	ID     string
	Rect   Rect
	FromMs int64
	ToMs   int64
	Class  string
	Follow bool
	Path   []MarkSample
}

// markShouldFollow reports whether TrackROI should run a side tracker.
// Exclude/source default to follow when Follow is true; region is label-only
// unless Follow was set explicitly (unusual).
func markShouldFollow(m SceneMark) bool {
	if !m.Follow {
		return false
	}
	switch m.Kind {
	case "exclude", "source":
		return m.Rect.W > 0 && m.Rect.H > 0
	default:
		return m.Rect.W > 0 && m.Rect.H > 0
	}
}

// sceneMarksShifted returns a copy of marks with FromMs/ToMs and Path
// sample times moved by deltaMs. The "whole clip" sentinel (FromMs = ToMs =
// 0) stays as is. TrackROI uses it to put absolute-time marks on the rhythm
// grid's clock, which counts from the first tracked frame (StartTimeSec).
func sceneMarksShifted(marks []SceneMark, deltaMs int64) []SceneMark {
	if deltaMs == 0 || len(marks) == 0 {
		return marks
	}
	out := make([]SceneMark, len(marks))
	for i, m := range marks {
		if m.FromMs != 0 || m.ToMs != 0 {
			m.FromMs += deltaMs
			m.ToMs += deltaMs
		}
		if len(m.Path) > 0 {
			path := make([]MarkSample, len(m.Path))
			for j, ps := range m.Path {
				ps.Ms += deltaMs
				path[j] = ps
			}
			m.Path = path
		}
		out[i] = m
	}
	return out
}

// sceneMarkActive reports whether m applies at time midMs.
func sceneMarkActive(m SceneMark, midMs int64) bool {
	if m.FromMs == 0 && m.ToMs == 0 {
		return true
	}
	return midMs >= m.FromMs && midMs <= m.ToMs
}

// sceneMarkRectAt returns the mark box at midMs: nearest Path sample when
// present, otherwise the static Rect. Root cause of "mark stays fixed":
// older marks had only Rect and no Path/Follow.
func sceneMarkRectAt(m SceneMark, midMs int64) Rect {
	if len(m.Path) == 0 {
		return m.Rect
	}
	best := m.Path[0]
	bestDist := abs64(midMs - best.Ms)
	for i := 1; i < len(m.Path); i++ {
		d := abs64(midMs - m.Path[i].Ms)
		if d < bestDist {
			best, bestDist = m.Path[i], d
		}
	}
	return best.Rect
}

func abs64(v int64) int64 {
	if v < 0 {
		return -v
	}
	return v
}

func pointInRect(px, py float64, r Rect) bool {
	return px >= float64(r.X) && px < float64(r.X+r.W) &&
		py >= float64(r.Y) && py < float64(r.Y+r.H)
}

// cameraExcludeRects builds the feature punch-out list for camera motion:
// tracked box plus active exclude SceneMarks (incl. soft masks converted
// to exclude at the call site). Uses Follow Path when present.
func cameraExcludeRects(tracked Rect, marks []SceneMark, atMs int64) []Rect {
	out := make([]Rect, 0, 1+len(marks))
	out = append(out, tracked)
	for _, mk := range marks {
		if mk.Kind != "exclude" || !sceneMarkActive(mk, atMs) {
			continue
		}
		out = append(out, sceneMarkRectAt(mk, atMs))
	}
	return out
}
