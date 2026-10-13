package generator

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// ContactCandidatePrefix marks scene-map regions written by
// ImportContactCandidates, so a re-import can replace its own unreviewed
// candidates without touching anything a person drew or confirmed.
const ContactCandidatePrefix = "teacher-contact-"

// ContactCandidateOptions tunes ImportContactCandidates.
type ContactCandidateOptions struct {
	// MinAgree: only points at least this many teachers agreed on become
	// candidates (default 2 - the Owner's "several models per video").
	MinAgree int
	// MinGapMs: at most one candidate per this many ms (default 2000), so a
	// clip yields a reviewable handful, not every 0.5 s sample.
	MinGapMs int64
	// BoxCells: side of the box drawn around a point that has no teacher
	// box, in rhythm-grid cells (default 2).
	BoxCells float64
}

type contactCandidateFile struct {
	Version int `json:"version"`
	Points  []struct {
		TMs    int64     `json:"t_ms"`
		X      float64   `json:"x"`
		Y      float64   `json:"y"`
		Agree  int       `json:"agree"`
		Source string    `json:"source"`
		Box    []float64 `json:"box"`
	} `json:"points"`
	Teachers []string `json:"teachers"`
}

// ImportContactCandidates turns the teacher consensus of a
// contact_points.py result into scene-map region marks of class "contact" in
// a .samn, for the user to confirm or reject. Candidates are written
// author:"auto", reviewed:false, so the P5c export (reviewedYOLOMarks) keeps
// them out of any training set until a person confirms them (SceneMap plan
// § 6 decision 3). Region marks have no engine effect.
//
// Re-importing replaces earlier candidates that are not confirmed
// (reviewed:false); confirmed ones (reviewed:true) stay, and no new
// candidate is added within MinGapMs of them. Returns how many candidates
// were written.
func ImportContactCandidates(samnPath, contactPath string, o ContactCandidateOptions) (int, error) {
	if o.MinAgree <= 0 {
		o.MinAgree = 2
	}
	if o.MinGapMs <= 0 {
		o.MinGapMs = 2000
	}
	if o.BoxCells <= 0 {
		o.BoxCells = 2
	}
	b, err := os.ReadFile(contactPath)
	if err != nil {
		return 0, err
	}
	var f contactCandidateFile
	if err := json.Unmarshal(b, &f); err != nil {
		return 0, fmt.Errorf("contact points %s: %w", contactPath, err)
	}
	if f.Version != 1 {
		return 0, fmt.Errorf("contact points %s: unsupported version %d", contactPath, f.Version)
	}
	doc, err := samn.Load(samnPath)
	if err != nil {
		return 0, err
	}
	sm := doc.SceneMap
	if sm == nil || sm.Video.Width <= 0 || sm.Video.Height <= 0 {
		return 0, fmt.Errorf("generator: %s has no scene map with video size - run Generate with the rhythm grid first", samnPath)
	}
	w, h := float64(sm.Video.Width), float64(sm.Video.Height)
	cols := sm.Grid.Cols
	if cols <= 0 {
		cols = 16
	}
	side := o.BoxCells * w / float64(cols)

	kept := make([]funscript.SceneMapMark, 0, len(sm.Marks))
	var reviewedAt []int64
	for _, m := range sm.Marks {
		if strings.HasPrefix(m.ID, ContactCandidatePrefix) {
			if m.Reviewed == nil || !*m.Reviewed {
				continue // not confirmed: replaced by this import
			}
			if m.AtMs != nil {
				reviewedAt = append(reviewedAt, *m.AtMs)
			}
		}
		kept = append(kept, m)
	}

	points := f.Points
	sort.SliceStable(points, func(i, j int) bool { return points[i].TMs < points[j].TMs })
	nTeachers := len(f.Teachers)
	added := 0
	last := int64(math.MinInt64 / 2)
	for _, p := range points {
		if p.Agree < o.MinAgree || p.TMs < 0 || p.X < 0 || p.X > 1 || p.Y < 0 || p.Y > 1 {
			continue
		}
		if p.TMs-last < o.MinGapMs || nearAny(p.TMs, reviewedAt, o.MinGapMs) {
			continue
		}
		var x0, y0, x1, y1 float64
		if len(p.Box) == 4 && p.Box[2] > p.Box[0] && p.Box[3] > p.Box[1] {
			x0, y0, x1, y1 = p.Box[0]*w, p.Box[1]*h, p.Box[2]*w, p.Box[3]*h
		} else {
			cx, cy := p.X*w, p.Y*h
			x0, y0, x1, y1 = cx-side/2, cy-side/2, cx+side/2, cy+side/2
		}
		x0, y0 = math.Max(0, x0), math.Max(0, y0)
		x1, y1 = math.Min(w, x1), math.Min(h, y1)
		if x1-x0 < 2 || y1-y0 < 2 {
			continue
		}
		at := p.TMs
		unreviewed := false
		conf := 1.0
		if nTeachers > 0 {
			conf = math.Min(1, float64(p.Agree)/float64(nTeachers))
		}
		kept = append(kept, funscript.SceneMapMark{
			ID:         fmt.Sprintf("%s%d", ContactCandidatePrefix, at),
			Kind:       "region",
			Class:      "contact",
			Author:     "auto",
			Rect:       []int{int(math.Round(x0)), int(math.Round(y0)), int(math.Round(x1 - x0)), int(math.Round(y1 - y0))},
			AtMs:       &at,
			Confidence: math.Round(conf*100) / 100,
			Reviewed:   &unreviewed,
		})
		last = at
		added++
	}
	sm.Marks = kept
	if err := samn.Save(samnPath, doc); err != nil {
		return 0, err
	}
	return added, nil
}

func nearAny(t int64, ts []int64, gap int64) bool {
	for _, v := range ts {
		if d := t - v; d > -gap && d < gap {
			return true
		}
	}
	return false
}
