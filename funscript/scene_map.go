package funscript

import "encoding/base64"

// SceneMapData is optional metadata.scene_map (docs/SCENE_MAP_PLAN.md M4).
// Written only when a rhythm SceneMap exists. Additive; old readers ignore.
// Community .funscript export must not carry this block (size).
type SceneMapData struct {
	Version int              `json:"version"`
	Video   SceneMapVideo    `json:"video"`
	Grid    SceneMapGrid     `json:"grid"`
	Windows []SceneMapWindow `json:"windows,omitempty"`
	Marks   []SceneMapMark   `json:"marks,omitempty"`
	Events  []SceneMapEvent  `json:"events,omitempty"`
}

// SceneMapVideo identifies the clip without storing media.
// Sha256Head is the hex SHA-256 of the first MiB of the video file.
type SceneMapVideo struct {
	DurationMs int64  `json:"durationMs"`
	Width      int    `json:"width"`
	Height     int    `json:"height"`
	Sha256Head string `json:"sha256_head,omitempty"`
}

// SceneMapGrid is the rhythm-grid geometry (typically 16 × aspect rows).
type SceneMapGrid struct {
	Cols int `json:"cols"`
	Rows int `json:"rows"`
}

// SceneMapWindow is one scoring window. ScoreB64 is standard base64 of the
// per-cell Score []uint8 (cols*rows, normalized 0–255).
type SceneMapWindow struct {
	StartMs  int64     `json:"startMs"`
	EndMs    int64     `json:"endMs"`
	TempoHz  float64   `json:"tempoHz"`
	ScoreB64 string    `json:"score_b64"`
	Chosen   int       `json:"chosen"` // -1 = tracker fallback / unknown
	SignRule string    `json:"signRule,omitempty"`
	TrackerR float64   `json:"trackerR,omitempty"`
	Box      []float64 `json:"box,omitempty"` // [cx, cy] CSRT centre
	Marks    []string  `json:"marks,omitempty"`
}

// SceneMapMark is a user/auto annotation (exclude / source / region).
// Author "auto" marks should carry Confidence and Reviewed (M5 training gate).
// Follow=true means the engine tracked the box with the subject; Path holds
// sparse samples for learning / replay overlays.
type SceneMapMark struct {
	ID         string               `json:"id"`
	Kind       string               `json:"kind"`
	Rect       []int                `json:"rect"` // [x, y, w, h]
	FromMs     int64                `json:"fromMs,omitempty"`
	ToMs       int64                `json:"toMs,omitempty"`
	AtMs       *int64               `json:"atMs,omitempty"`
	Author     string               `json:"author,omitempty"`
	CreatedAt  string               `json:"createdAt,omitempty"`
	Class      string               `json:"class,omitempty"`
	Role       string               `json:"role,omitempty"`
	Confidence float64              `json:"confidence,omitempty"`
	Reviewed   *bool                `json:"reviewed,omitempty"`
	Follow     bool                 `json:"follow,omitempty"`
	Path       []SceneMapMarkSample `json:"path,omitempty"`
}

// SceneMapMarkSample is one Follow-path rect at Ms.
type SceneMapMarkSample struct {
	Ms   int64 `json:"ms"`
	Rect []int `json:"rect"` // [x,y,w,h]
}

// SceneMapEvent records engine incidents (reacquire, …). Optional in P4.
type SceneMapEvent struct {
	AtMs    int64  `json:"atMs"`
	Kind    string `json:"kind"`
	FromBox []int  `json:"fromBox,omitempty"`
	ToBox   []int  `json:"toBox,omitempty"`
}

// EncodeSceneMapScore returns standard base64 of score bytes.
func EncodeSceneMapScore(score []uint8) string {
	if len(score) == 0 {
		return ""
	}
	return base64.StdEncoding.EncodeToString(score)
}

// DecodeSceneMapScore decodes score_b64 back to per-cell scores.
func DecodeSceneMapScore(s string) ([]uint8, error) {
	if s == "" {
		return nil, nil
	}
	return base64.StdEncoding.DecodeString(s)
}

// HasWindows reports whether a persistable map is present.
func (m *SceneMapData) HasWindows() bool {
	return m != nil && len(m.Windows) > 0
}
