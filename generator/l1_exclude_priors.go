package generator

import (
	"bufio"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// L1 prior gates (docs/SCENE_MAP_PLAN.md M6-L1). Suggest-only — never
// auto-applied to Create. Need enough Collect exports before anything fires.
const (
	l1PriorGridCols = 4
	l1PriorGridRows = 3
	// Min distinct clips with ≥1 exclude before any cell may suggest.
	L1PriorMinClips = 3
	// Cell must appear in at least this share of clips that have excludes.
	L1PriorMinShare = 0.50
	// Max suggestions returned (hottest cells first).
	L1PriorMaxSuggest = 4
)

// ExcludePriorSuggestion is one suggest-only Ignore mark for the current video.
type ExcludePriorSuggestion struct {
	Mark       SceneMark `json:"mark"`
	Share      float64   `json:"share"`     // fraction of exclude-clips hitting this cell
	ClipCount  int       `json:"clipCount"` // distinct clips contributing
	CellCol    int       `json:"cellCol"`
	CellRow    int       `json:"cellRow"`
	Confidence float64   `json:"confidence"` // == Share for L1 (simple stats)
}

// ExcludePriorResult is the L1 suggest payload for Advanced Scene map.
type ExcludePriorResult struct {
	Suggestions []ExcludePriorSuggestion `json:"suggestions,omitempty"`
	ClipsSeen   int                      `json:"clipsSeen"`
	Decisions   int                      `json:"decisions"`
	Note        string                   `json:"note,omitempty"`
	LearningDir string                   `json:"learningDir,omitempty"`
}

// SuggestExcludePriors scans scene_map_learning/**/exclude_decisions.jsonl
// and returns pre-filled Ignore marks scaled to targetW×targetH.
// Empty Suggestions + Note when Collect data is insufficient — never errors
// for "not enough data". LearningDir defaults to DefaultSceneMapLearningDir().
func SuggestExcludePriors(targetW, targetH int, learningDir string) (ExcludePriorResult, error) {
	out := ExcludePriorResult{}
	if targetW <= 0 || targetH <= 0 {
		return out, fmt.Errorf("generator: L1 suggest needs positive video size")
	}
	if learningDir == "" {
		learningDir = DefaultSceneMapLearningDir()
	}
	out.LearningDir = learningDir
	if learningDir == "" {
		out.Note = "No learning directory configured."
		return out, nil
	}
	st, err := os.Stat(learningDir)
	if err != nil || !st.IsDir() {
		out.Note = "No Collect exports yet — Settings → Collect learning data, then Export for learning on a few clips."
		return out, nil
	}

	type clipStats struct {
		w, h  int
		cells map[int]bool // grid cell index → hit
		nDec  int
	}
	clips := map[string]*clipStats{}

	err = filepath.WalkDir(learningDir, func(path string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return nil // skip broken trees
		}
		if d.IsDir() || d.Name() != "exclude_decisions.jsonl" {
			return nil
		}
		clipDir := filepath.Dir(path)
		clipKey := filepath.Base(clipDir)
		vw, vh := readLearningVideoSize(clipDir)
		if vw <= 0 || vh <= 0 {
			vw, vh = 1280, 720 // last-resort normalize; better than skip
		}
		cs := &clipStats{w: vw, h: vh, cells: map[int]bool{}}
		n, cellHits := readExcludeDecisionCells(path, vw, vh)
		cs.nDec = n
		for _, c := range cellHits {
			cs.cells[c] = true
		}
		if cs.nDec > 0 {
			clips[clipKey] = cs
			out.Decisions += cs.nDec
		}
		return nil
	})
	if err != nil {
		return out, err
	}
	out.ClipsSeen = len(clips)
	if out.ClipsSeen < L1PriorMinClips {
		out.Note = fmt.Sprintf(
			"Need ≥%d clips with Ignore exports (have %d). Export for learning on more clips first.",
			L1PriorMinClips, out.ClipsSeen)
		return out, nil
	}

	// Count clips per cell.
	cellHits := make([]int, l1PriorGridCols*l1PriorGridRows)
	for _, cs := range clips {
		for c := range cs.cells {
			if c >= 0 && c < len(cellHits) {
				cellHits[c]++
			}
		}
	}

	type ranked struct {
		cell  int
		count int
		share float64
	}
	var hot []ranked
	nClips := float64(out.ClipsSeen)
	for c, count := range cellHits {
		share := float64(count) / nClips
		if count >= L1PriorMinClips && share+1e-9 >= L1PriorMinShare {
			hot = append(hot, ranked{cell: c, count: count, share: share})
		}
	}
	if len(hot) == 0 {
		out.Note = fmt.Sprintf(
			"No Ignored region shared by ≥%.0f%% of %d clips yet — keep Collect + Export.",
			L1PriorMinShare*100, out.ClipsSeen)
		return out, nil
	}
	sort.Slice(hot, func(i, j int) bool {
		if hot[i].share != hot[j].share {
			return hot[i].share > hot[j].share
		}
		return hot[i].count > hot[j].count
	})
	if len(hot) > L1PriorMaxSuggest {
		hot = hot[:L1PriorMaxSuggest]
	}

	out.Suggestions = make([]ExcludePriorSuggestion, 0, len(hot))
	for i, h := range hot {
		col := h.cell % l1PriorGridCols
		row := h.cell / l1PriorGridCols
		rect := cellRectPixels(col, row, targetW, targetH)
		out.Suggestions = append(out.Suggestions, ExcludePriorSuggestion{
			Mark: SceneMark{
				ID:     fmt.Sprintf("suggest%d", i+1),
				Kind:   "exclude",
				Rect:   rect,
				FromMs: 0,
				ToMs:   0, // whole clip
				Follow: true,
				Author: "suggest",
			},
			Share:      h.share,
			ClipCount:  h.count,
			CellCol:    col,
			CellRow:    row,
			Confidence: h.share,
		})
	}
	out.Note = fmt.Sprintf(
		"Suggested %d Ignore region(s) from %d clips — review on the map; Clear removes them. Not applied until Create.",
		len(out.Suggestions), out.ClipsSeen)
	return out, nil
}

type excludeDecisionLine struct {
	Kind string `json:"kind"`
	Rect []int  `json:"rect"`
}

func readLearningVideoSize(clipDir string) (w, h int) {
	raw, err := os.ReadFile(filepath.Join(clipDir, "source.json"))
	if err != nil {
		return 0, 0
	}
	var meta struct {
		Video struct {
			Width  int `json:"width"`
			Height int `json:"height"`
		} `json:"video"`
	}
	if json.Unmarshal(raw, &meta) != nil {
		return 0, 0
	}
	return meta.Video.Width, meta.Video.Height
}

func readExcludeDecisionCells(path string, vw, vh int) (n int, cells []int) {
	f, err := os.Open(path)
	if err != nil {
		return 0, nil
	}
	defer f.Close()
	seen := map[int]bool{}
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 64*1024), 4*1024*1024)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		var rec excludeDecisionLine
		if json.Unmarshal([]byte(line), &rec) != nil {
			continue
		}
		if strings.ToLower(rec.Kind) != "exclude" {
			continue
		}
		if len(rec.Rect) < 4 || rec.Rect[2] <= 0 || rec.Rect[3] <= 0 {
			continue
		}
		n++
		for _, c := range rectToPriorCells(rec.Rect, vw, vh) {
			if !seen[c] {
				seen[c] = true
				cells = append(cells, c)
			}
		}
	}
	return n, cells
}

// rectToPriorCells returns coarse-grid cells overlapped by rect (pixel space).
func rectToPriorCells(rect []int, vw, vh int) []int {
	if vw <= 0 || vh <= 0 || len(rect) < 4 {
		return nil
	}
	x0 := float64(rect[0]) / float64(vw)
	y0 := float64(rect[1]) / float64(vh)
	x1 := float64(rect[0]+rect[2]) / float64(vw)
	y1 := float64(rect[1]+rect[3]) / float64(vh)
	x0 = clamp01(x0)
	y0 = clamp01(y0)
	x1 = clamp01(x1)
	y1 = clamp01(y1)
	if x1 <= x0 || y1 <= y0 {
		return nil
	}
	c0 := int(math.Floor(x0 * float64(l1PriorGridCols)))
	c1 := int(math.Ceil(x1*float64(l1PriorGridCols))) - 1
	r0 := int(math.Floor(y0 * float64(l1PriorGridRows)))
	r1 := int(math.Ceil(y1*float64(l1PriorGridRows))) - 1
	if c0 < 0 {
		c0 = 0
	}
	if r0 < 0 {
		r0 = 0
	}
	if c1 >= l1PriorGridCols {
		c1 = l1PriorGridCols - 1
	}
	if r1 >= l1PriorGridRows {
		r1 = l1PriorGridRows - 1
	}
	var out []int
	for r := r0; r <= r1; r++ {
		for c := c0; c <= c1; c++ {
			out = append(out, r*l1PriorGridCols+c)
		}
	}
	return out
}

func cellRectPixels(col, row, tw, th int) ROI {
	cellW := float64(tw) / float64(l1PriorGridCols)
	cellH := float64(th) / float64(l1PriorGridRows)
	// Inset slightly so suggestions don't cover the whole frame edge-to-edge.
	padX := cellW * 0.08
	padY := cellH * 0.08
	x := int(math.Round(float64(col)*cellW + padX))
	y := int(math.Round(float64(row)*cellH + padY))
	w := int(math.Round(cellW - 2*padX))
	h := int(math.Round(cellH - 2*padY))
	if w < 8 {
		w = 8
	}
	if h < 8 {
		h = 8
	}
	if x+w > tw {
		w = tw - x
	}
	if y+h > th {
		h = th - y
	}
	return ROI{X: x, Y: y, W: w, H: h}
}

func clamp01(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}
