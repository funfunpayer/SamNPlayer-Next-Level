package generator

import (
	"os"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// SceneMapLoad is the Create-side restore payload from a companion .samn
// (docs/SCENE_MAP_PLAN.md M4 / P4 follow-up). Empty Map + Marks means nothing
// to restore; callers should not treat that as an error.
type SceneMapLoad struct {
	Path  string      `json:"path,omitempty"`
	Map   SceneMapDTO `json:"map"`
	Marks []SceneMark `json:"marks,omitempty"`
	Found bool        `json:"found"`
}

// LoadSceneMapBesideVideo reads companion .samn next to videoPath and returns
// any persisted sceneMap (windows + marks). Missing file / no sceneMap → Found false.
// Does not change Generate defaults.
func LoadSceneMapBesideVideo(videoPath string) (SceneMapLoad, error) {
	videoPath = strings.TrimSpace(videoPath)
	if videoPath == "" {
		return SceneMapLoad{}, nil
	}
	path := samn.CompanionSamnPath(videoPath)
	st, err := os.Stat(path)
	if err != nil || st.IsDir() {
		return SceneMapLoad{}, nil
	}
	doc, err := samn.Load(path)
	if err != nil {
		return SceneMapLoad{}, err
	}
	if doc.SceneMap == nil || (!doc.SceneMap.HasWindows() && len(doc.SceneMap.Marks) == 0) {
		return SceneMapLoad{Path: path}, nil
	}
	dto, marks, err := SceneMapFromPersist(doc.SceneMap)
	if err != nil {
		return SceneMapLoad{}, err
	}
	return SceneMapLoad{
		Path:  path,
		Map:   dto,
		Marks: marks,
		Found: len(dto.Windows) > 0 || len(marks) > 0,
	}, nil
}

// SceneMapFromPersist converts .samn sceneMap (score_b64, rect slices) back to
// the GUI/Generate shape (score []uint8, SceneMark with ROI).
func SceneMapFromPersist(m *funscript.SceneMapData) (SceneMapDTO, []SceneMark, error) {
	if m == nil {
		return SceneMapDTO{}, nil, nil
	}
	out := SceneMapDTO{
		Version: m.Version,
		Cols:    m.Grid.Cols,
		Rows:    m.Grid.Rows,
		Width:   m.Video.Width,
		Height:  m.Video.Height,
		Windows: make([]MapWindowDTO, 0, len(m.Windows)),
	}
	if out.Version == 0 {
		out.Version = 1
	}
	for _, w := range m.Windows {
		score, err := funscript.DecodeSceneMapScore(w.ScoreB64)
		if err != nil {
			return SceneMapDTO{}, nil, err
		}
		mw := MapWindowDTO{
			StartMs:    w.StartMs,
			EndMs:      w.EndMs,
			TempoHz:    w.TempoHz,
			Score:      score,
			ChosenCell: w.Chosen,
			SignRule:   w.SignRule,
			TrackerR:   w.TrackerR,
			Marks:      append([]string(nil), w.Marks...),
		}
		if len(w.Box) >= 2 {
			mw.BoxCX, mw.BoxCY = w.Box[0], w.Box[1]
		}
		out.Windows = append(out.Windows, mw)
	}
	return out, sceneMarksFromPersist(m.Marks), nil
}

func sceneMarksFromPersist(marks []funscript.SceneMapMark) []SceneMark {
	if len(marks) == 0 {
		return nil
	}
	out := make([]SceneMark, 0, len(marks))
	for _, m := range marks {
		if m.Kind == "" && m.ID == "" {
			continue
		}
		sm := SceneMark{
			ID:         m.ID,
			Kind:       m.Kind,
			FromMs:     m.FromMs,
			ToMs:       m.ToMs,
			Class:      m.Class,
			Author:     m.Author,
			Confidence: m.Confidence,
			Follow:     m.Follow,
		}
		if m.AtMs != nil {
			at := *m.AtMs
			sm.AtMs = &at
		}
		if m.Reviewed != nil {
			v := *m.Reviewed
			sm.Reviewed = &v
		}
		if len(m.Rect) >= 4 {
			sm.Rect = ROI{X: m.Rect[0], Y: m.Rect[1], W: m.Rect[2], H: m.Rect[3]}
		}
		if len(m.Path) > 0 {
			sm.Path = make([]MarkSample, 0, len(m.Path))
			for _, p := range m.Path {
				if len(p.Rect) < 4 {
					continue
				}
				sm.Path = append(sm.Path, MarkSample{
					Ms:   p.Ms,
					Rect: ROI{X: p.Rect[0], Y: p.Rect[1], W: p.Rect[2], H: p.Rect[3]},
				})
			}
		}
		out = append(out, sm)
	}
	if len(out) == 0 {
		return nil
	}
	return out
}
