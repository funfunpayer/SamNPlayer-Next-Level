package generator

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
	"os"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

const sceneMapShaHeadBytes = 1 << 20 // first MiB

// buildSceneMapMeta builds the optional .samn scene_map block from a rhythm
// SceneMapDTO (engine by-product) plus Options.SceneMarks. Returns nil when
// there are no windows (nothing to persist).
func buildSceneMapMeta(videoPath string, durationMs int64, m SceneMapDTO, marks []SceneMark) *funscript.SceneMapData {
	if len(m.Windows) == 0 {
		return nil
	}
	out := &funscript.SceneMapData{
		Version: m.Version,
		Video: funscript.SceneMapVideo{
			DurationMs: durationMs,
			Width:      m.Width,
			Height:     m.Height,
			Sha256Head: videoSha256Head(videoPath),
		},
		Grid: funscript.SceneMapGrid{
			Cols: m.Cols,
			Rows: m.Rows,
		},
		Windows: make([]funscript.SceneMapWindow, len(m.Windows)),
		Marks:   sceneMarksToPersist(marks),
	}
	if out.Version == 0 {
		out.Version = 1
	}
	for i, w := range m.Windows {
		sw := funscript.SceneMapWindow{
			StartMs:  w.StartMs,
			EndMs:    w.EndMs,
			TempoHz:  w.TempoHz,
			ScoreB64: funscript.EncodeSceneMapScore(w.Score),
			Chosen:   w.ChosenCell,
			SignRule: w.SignRule,
			TrackerR: w.TrackerR,
			Marks:    append([]string(nil), w.Marks...),
		}
		if w.BoxCX != 0 || w.BoxCY != 0 {
			sw.Box = []float64{w.BoxCX, w.BoxCY}
		}
		out.Windows[i] = sw
	}
	return out
}

func sceneMarksToPersist(marks []SceneMark) []funscript.SceneMapMark {
	if len(marks) == 0 {
		return nil
	}
	out := make([]funscript.SceneMapMark, 0, len(marks))
	for _, m := range marks {
		if m.Kind == "" && m.ID == "" {
			continue
		}
		author := m.Author
		if author == "" {
			author = "user"
		}
		pm := funscript.SceneMapMark{
			ID:         m.ID,
			Kind:       m.Kind,
			Rect:       []int{m.Rect.X, m.Rect.Y, m.Rect.W, m.Rect.H},
			FromMs:     m.FromMs,
			ToMs:       m.ToMs,
			Class:      m.Class,
			Author:     author,
			Confidence: m.Confidence,
			Follow:     m.Follow,
		}
		if m.AtMs != nil {
			at := *m.AtMs
			pm.AtMs = &at
		}
		if m.Reviewed != nil {
			v := *m.Reviewed
			pm.Reviewed = &v
		}
		if len(m.Path) > 0 {
			pm.Path = make([]funscript.SceneMapMarkSample, len(m.Path))
			for i, p := range m.Path {
				pm.Path[i] = funscript.SceneMapMarkSample{
					Ms:   p.Ms,
					Rect: []int{p.Rect.X, p.Rect.Y, p.Rect.W, p.Rect.H},
				}
			}
		}
		out = append(out, pm)
	}
	if len(out) == 0 {
		return nil
	}
	return out
}

// videoSha256Head returns hex(SHA-256(first MiB)) or "" on error / empty path.
func videoSha256Head(path string) string {
	if path == "" {
		return ""
	}
	f, err := os.Open(path)
	if err != nil {
		return ""
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.CopyN(h, f, sceneMapShaHeadBytes); err != nil && err != io.EOF {
		return ""
	}
	return hex.EncodeToString(h.Sum(nil))
}
