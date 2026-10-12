package generator

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
)

// ContactPoint is where a local teacher (NudeNet, a VLM probe, later our
// own detector) saw the stroke contact: video time Ms and a normalized
// frame position 0..1. Written by generator/contact_points.py, used by the
// rhythm grid as its search centre when the CSRT box is out of reach
// (Options.ContactPointsFile, docs/VLM_TEACHER_PLAN.md VLM1).
type ContactPoint struct {
	Ms   int64
	X, Y float64
}

// contactPointsFile is the JSON written by contact_points.py.
type contactPointsFile struct {
	Version int `json:"version"`
	Points  []struct {
		TMs    int64   `json:"t_ms"`
		X      float64 `json:"x"`
		Y      float64 `json:"y"`
		Agree  int     `json:"agree"`
		Source string  `json:"source"`
	} `json:"points"`
}

// LoadContactPoints reads a contact_points.py result and keeps the points
// at least minAgree teachers agreed on (minAgree <= 1 keeps all). Points
// are returned sorted by time; points outside 0..1 are an error, since
// they would mean a coordinate bug upstream rather than a usable hint.
func LoadContactPoints(path string, minAgree int) ([]ContactPoint, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var f contactPointsFile
	if err := json.Unmarshal(b, &f); err != nil {
		return nil, fmt.Errorf("contact points %s: %w", path, err)
	}
	if f.Version != 1 {
		return nil, fmt.Errorf("contact points %s: unsupported version %d", path, f.Version)
	}
	out := make([]ContactPoint, 0, len(f.Points))
	for i, p := range f.Points {
		if p.X < 0 || p.X > 1 || p.Y < 0 || p.Y > 1 || p.TMs < 0 {
			return nil, fmt.Errorf("contact points %s: point %d out of range (t=%d x=%v y=%v)",
				path, i, p.TMs, p.X, p.Y)
		}
		agree := p.Agree
		if agree < 1 {
			agree = 1
		}
		if agree < minAgree {
			continue
		}
		out = append(out, ContactPoint{Ms: p.TMs, X: p.X, Y: p.Y})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Ms < out[j].Ms })
	return out, nil
}
