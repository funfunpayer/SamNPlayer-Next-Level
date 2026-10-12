package generator

// SceneMapDTO is the JSON-friendly scene-map payload for GUI / .samn (M1).
// Full TrackROI runs and ScanSceneMap both produce this shape.
type SceneMapDTO struct {
	Version int            `json:"version"`
	Cols    int            `json:"cols"`
	Rows    int            `json:"rows"`
	Width   int            `json:"width"`
	Height  int            `json:"height"`
	Windows []MapWindowDTO `json:"windows"`
}

// MapWindowDTO is one scoring window on the rhythm grid.
type MapWindowDTO struct {
	StartMs    int64    `json:"startMs"`
	EndMs      int64    `json:"endMs"`
	TempoHz    float64  `json:"tempoHz"`
	Score      []uint8  `json:"score"`      // cols*rows, 0–255
	ChosenCell int      `json:"chosenCell"` // -1 if unknown / quick scan
	BoxCX      float64  `json:"boxCx,omitempty"`
	BoxCY      float64  `json:"boxCy,omitempty"`
	SignRule   string   `json:"signRule,omitempty"`
	TrackerR   float64  `json:"trackerR,omitempty"`
	Marks      []string `json:"marks,omitempty"` // active exclude/source IDs (M3)
}

// DefaultSceneMapWindows is the plan default for ScanSceneMap (6 × 8s).
const DefaultSceneMapWindows = 6
