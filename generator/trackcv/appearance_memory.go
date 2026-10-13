//go:build cgo && opencv

package trackcv

// appearanceMemory ist die Go-Entsprechung von generate_funscript.py's
// AppearanceMemory: merkt sich in Abständen kleine Graustufen-Ausschnitte
// der verfolgten Region, und findet sie per Template-Matching im ganzen
// Bild wieder, wenn der Tracker das Ziel verliert oder ein Szenenschnitt
// kommt - siehe dortiger Klassenkommentar für die Begründung (ein
// Objekterkenner-Ersatz ohne Modell).
type appearanceMemory struct {
	maxTemplates int
	minScore     float64
	downscale    float64
	templates    []*Gray
}

func newAppearanceMemory() *appearanceMemory {
	return &appearanceMemory{maxTemplates: 8, minScore: 0.55, downscale: 0.5}
}

func (m *appearanceMemory) close() {
	for _, t := range m.templates {
		t.Close()
	}
	m.templates = nil
}

func (m *appearanceMemory) remember(gray *Gray, box Rect) {
	if box.W <= 0 || box.H <= 0 {
		return
	}
	tmpl := gray.ExtractTemplate(box, m.downscale)
	if tmpl == nil {
		return
	}
	m.templates = append(m.templates, tmpl)
	if len(m.templates) > m.maxTemplates {
		// Das ÄLTESTE verwerfen, aber den ERSTEN (Index 0, die vom Nutzer
		// bestätigte Startregion) behalten - siehe Python-Original.
		m.templates[1].Close()
		m.templates = append(m.templates[:1], m.templates[2:]...)
	}
}

// matchesOriginal reports how well box (the tracker's CURRENT belief, in
// full-resolution gray coordinates) still resembles templates[0] - the
// very first remembered crop, kept forever by remember() specifically as
// the "user-confirmed start region" (see its comment above).
//
// Why this exists: TrackROI's CSRT can drift gradually off the true
// target over a long continuous run (a handful/no single frame ever
// moves far enough to look wrong, but the box still ends up somewhere
// else entirely after a couple of minutes - confirmed on a real clip,
// docs/AGENT_COORD.md 23 Sep "CSRT long-clip drift"). remember() runs
// periodically on whatever the tracker CURRENTLY believes, with no check
// against reality, so once drift sets in, the memory bank keeps filling
// with crops of the wrong region. When the tracker then genuinely loses
// the target (which it eventually did on that same clip), reacquire()
// searches for a match to those poisoned templates and confidently
// re-anchors on the SAME wrong region - not a bug in reacquire() itself,
// it faithfully found what it was told to look for.
//
// ok=false means "nothing to compare against yet" (no templates
// remembered) - callers should treat that as "allow it", not "reject
// it": there's no basis for suspicion this early.
func (m *appearanceMemory) matchesOriginal(gray *Gray, box Rect) (score float64, ok bool) {
	if len(m.templates) == 0 {
		return 0, false
	}
	orig := m.templates[0]
	// The search window is sized off orig's OWN dimensions, not box's -
	// CSRT's scale estimate can drift independently of position (found
	// investigating this same real clip: the box shrank from its
	// original ~171x216 down to ~50x65 over ~175s of continuous
	// tracking). Sizing the window off box would make it shrink right
	// along with it, and once smaller than orig, the size check below
	// would always fail closed to "unknown" - silently disabling this
	// entire check exactly when the tracker's belief has drifted most.
	// Centered on box's current center (position, unlike size, stayed a
	// reasonable anchor throughout).
	scale := 1.0
	if m.downscale != 0 {
		scale = 1.0 / m.downscale
	}
	origW := int(float64(orig.Width()) * scale)
	origH := int(float64(orig.Height()) * scale)
	searchW := origW*2 + 16
	searchH := origH*2 + 16
	cx, cy := box.X+box.W/2, box.Y+box.H/2
	region := gray.ExtractTemplate(Rect{
		X: cx - searchW/2, Y: cy - searchH/2,
		W: searchW, H: searchH,
	}, m.downscale)
	if region == nil {
		return 0, false
	}
	defer region.Close()
	if orig.Width() >= region.Width() || orig.Height() >= region.Height() {
		return 0, false
	}
	_, _, matchScore, matched := MatchTemplate(region, orig)
	return matchScore, matched
}

// reacquireCand is one full-resolution match above minScore.
type reacquireCand struct {
	score float64
	box   Rect
}

// reacquire sucht die Region im ganzen Bild. ok=false, wenn nichts
// Verlässliches gefunden wurde (unterhalb minScore wird NICHT neu
// verankert - eine geratene Position ist schlechter als die alte).
//
// When templates[0] exists (user-confirmed start / frame-0 seed), a
// candidate must positively resemble that seed (matchesOriginal known and
// ≥ minScore). Unlike remember()'s "unknown → allow" gate, reacquire fails
// closed: an ambiguous / non-matching candidate is skipped so the tip/partner
// caller can coast instead of hard-tipping onto a poisoned bank match
// (TrackROI already gated remember(); tip paths now share that gate plus
// this reject path).
func (m *appearanceMemory) reacquire(gray *Gray) (box Rect, ok bool) {
	cands := m.reacquireCandidates(gray)
	for _, c := range cands {
		score, known := m.matchesOriginal(gray, c.box)
		if !known || score < m.minScore {
			continue
		}
		return c.box, true
	}
	return Rect{}, false
}

// reacquireCandidates returns full-res matches sorted by score descending.
func (m *appearanceMemory) reacquireCandidates(gray *Gray) []reacquireCand {
	if len(m.templates) == 0 || gray == nil {
		return nil
	}
	frame := gray.Downscale(m.downscale)
	defer frame.Close()

	scale := 1.0
	if m.downscale != 0 {
		scale = 1.0 / m.downscale
	}

	var out []reacquireCand
	for _, tmpl := range m.templates {
		if tmpl.Height() >= frame.Height() || tmpl.Width() >= frame.Width() {
			continue
		}
		x, y, score, matched := MatchTemplate(frame, tmpl)
		if !matched || score < m.minScore {
			continue
		}
		out = append(out, reacquireCand{
			score: score,
			box: Rect{
				X: int(float64(x) * scale),
				Y: int(float64(y) * scale),
				W: maxInt(8, int(float64(tmpl.Width())*scale)),
				H: maxInt(8, int(float64(tmpl.Height())*scale)),
			},
		})
	}
	// Insertion sort by score desc — template count is tiny (≤ maxTemplates).
	for i := 1; i < len(out); i++ {
		j := i
		for j > 0 && out[j].score > out[j-1].score {
			out[j], out[j-1] = out[j-1], out[j]
			j--
		}
	}
	return out
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}
