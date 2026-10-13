package funscript

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
)

// Project is a lightweight SamNPlayer session sidecar (OFS-inspired, JSON).
// Stored next to the video or script as "<name>.snp.json".
type Project struct {
	Version      int           `json:"version"`
	VideoPath    string        `json:"videoPath,omitempty"`
	ScriptPath   string        `json:"scriptPath,omitempty"`
	OffsetMs     int64         `json:"offsetMs,omitempty"`
	SeekMs       int64         `json:"seekMs,omitempty"`
	LoopMarker   *Marker       `json:"loopMarker,omitempty"`
	Bookmarks    []Bookmark    `json:"bookmarks,omitempty"`
	Chapters     []ChapterMark `json:"chapters,omitempty"`
	LastOpenedMs int64         `json:"lastOpenedMs,omitempty"`
}

// Marker is a simple time range (playback loop / selection).
type Marker struct {
	StartMs int64 `json:"startMs"`
	EndMs   int64 `json:"endMs"`
}

const projectVersion = 1

// ProjectPathFor returns the sidecar path beside mediaPath.
func ProjectPathFor(mediaPath string) string {
	ext := filepath.Ext(mediaPath)
	base := mediaPath[:len(mediaPath)-len(ext)]
	return base + ".snp.json"
}

// SaveProject writes the sidecar atomically-ish (write then rename via temp).
func SaveProject(path string, p Project) error {
	if p.Version == 0 {
		p.Version = projectVersion
	}
	data, err := json.MarshalIndent(p, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(path, data, 0o644)
}

// LoadProject reads a sidecar.
func LoadProject(path string) (Project, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Project{}, err
	}
	var p Project
	if err := json.Unmarshal(data, &p); err != nil {
		return Project{}, fmt.Errorf("project: ungültiges JSON: %w", err)
	}
	if p.Version == 0 {
		p.Version = projectVersion
	}
	return p, nil
}

// SnapMs snaps t to the nearest frame for fps (OFS frame overlay aid).
// fps <= 0 returns t unchanged.
func SnapMs(t int64, fps float64) int64 {
	if fps <= 0 || math.IsNaN(fps) || math.IsInf(fps, 0) {
		return t
	}
	frame := math.Round(float64(t) * fps / 1000.0)
	return int64(math.Round(frame * 1000.0 / fps))
}

// DeleteRange removes actions with at in [start,end] (inclusive).
// Keeps at least endpoints if the range would empty the script — caller
// should validate; returns error if fewer than 2 points remain.
func DeleteRange(actions []Action, start, end int64) ([]Action, error) {
	if end < start {
		start, end = end, start
	}
	out := make([]Action, 0, len(actions))
	for _, a := range actions {
		if a.At < start || a.At > end {
			out = append(out, a)
		}
	}
	if len(out) < 2 {
		return nil, fmt.Errorf("funscript: Löschen würde weniger als 2 Punkte lassen")
	}
	return out, nil
}

// CapSpeedRange clamps segment intensity in [start,end] to maxIntensity
// by stretching time on too-fast segments; subsequent points shift right.
func CapSpeedRange(actions []Action, start, end int64, maxIntensity float64) ([]Action, error) {
	if maxIntensity <= 0 {
		maxIntensity = DefaultMaxIntensity
	}
	if len(actions) < 2 {
		return nil, fmt.Errorf("funscript: zu wenige Punkte")
	}
	if end < start {
		start, end = end, start
	}
	out := append([]Action(nil), actions...)
	// Overlap must use original timestamps — after the first stretch,
	// shifted At would fall past `end` and skip later in-range segments.
	origAt := make([]int64, len(out))
	for i := range out {
		origAt[i] = out[i].At
	}
	var shift int64
	for i := 0; i < len(out)-1; i++ {
		out[i].At = origAt[i] + shift
		nextAt := origAt[i+1] + shift
		dt := float64(nextAt - out[i].At)
		if dt <= 0 {
			continue
		}
		// Segment overlaps edit range on the ORIGINAL timeline.
		if origAt[i+1] < start || origAt[i] > end {
			continue
		}
		dpos := math.Abs(float64(out[i+1].Pos - out[i].Pos))
		inten := 500.0 * dpos / dt
		if inten <= maxIntensity {
			continue
		}
		need := 500.0 * dpos / maxIntensity
		extra := int64(math.Ceil(need - dt))
		if extra > 0 {
			shift += extra
		}
	}
	out[len(out)-1].At = origAt[len(out)-1] + shift
	return out, nil
}

// ScaleRangePos scales positions in [start,end] around 50 by factor.
func ScaleRangePos(actions []Action, start, end int64, factor float64) []Action {
	return ScaleRangePosFade(actions, start, end, factor, false)
}

// ScaleRangePosFade scales positions in [start,end] around 50 by factor.
// When softEdges is true, the effective factor ramps from 1 at the range
// ends to `factor` at the midpoint (gentle vib/suction edits).
func ScaleRangePosFade(actions []Action, start, end int64, factor float64, softEdges bool) []Action {
	if end < start {
		start, end = end, start
	}
	span := end - start
	out := append([]Action(nil), actions...)
	for i := range out {
		if out[i].At < start || out[i].At > end {
			continue
		}
		f := factor
		if softEdges && span > 0 {
			// 0 at edges → 1 at midpoint.
			mid := float64(start+end) / 2
			half := float64(span) / 2
			w := 1 - math.Abs(float64(out[i].At)-mid)/half
			if w < 0 {
				w = 0
			}
			f = 1 + (factor-1)*w
		}
		p := 50 + (float64(out[i].Pos)-50)*f
		if p < 0 {
			p = 0
		}
		if p > 100 {
			p = 100
		}
		out[i].Pos = int(math.Round(p))
	}
	return out
}
