package generator

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// SceneMapLearningSubdir is the only tree cleared by DeleteSceneMapLearningData.
// It sits under the ROI dataset root so hand YOLO bootstrap samples stay safe.
const SceneMapLearningSubdir = "scene_map_learning"

// ErrLearningExportNotOptedIn is returned when export is refused without
// --opt-in / Collect learning data (Owner: default OFF).
var ErrLearningExportNotOptedIn = fmt.Errorf("generator: learning export requires explicit opt-in (Collect learning data / --opt-in)")

// LearningExportOptions controls SceneMap P5a local export (L0 collect only).
type LearningExportOptions struct {
	// OptIn must be true — Settings CollectLearningData or CLI --opt-in.
	OptIn bool
	// OutputDir defaults to DefaultRoiDatasetDir()/scene_map_learning.
	OutputDir string
}

// LearningExportResult is a short summary of what was written.
type LearningExportResult struct {
	OutDir           string `json:"outDir"`
	Windows          int    `json:"windows"`
	Negatives        int    `json:"negatives"`
	ExcludeDecisions int    `json:"excludeDecisions"`
	AutoCandidates   int    `json:"autoCandidates"`
	UserRegionMarks  int    `json:"userRegionMarks"`
	YoloFrames       int    `json:"yoloFrames"`
	YoloLabels       int    `json:"yoloLabels"`
	TracePath        string `json:"tracePath"`
	NegativesPath    string `json:"negativesPath"`
	DecisionsPath    string `json:"decisionsPath"`
	AutoPath         string `json:"autoPath"`
	UserRegionsPath  string `json:"userRegionsPath"`
	YoloDir          string `json:"yoloDir"`
}

// DefaultSceneMapLearningDir is …/roi_training_dataset/scene_map_learning.
func DefaultSceneMapLearningDir() string {
	root := DefaultRoiDatasetDir()
	if root == "" {
		return ""
	}
	return filepath.Join(root, SceneMapLearningSubdir)
}

// ExportSceneMapLearning walks one .samn with sceneMap and writes local
// JSON/JSONL artifacts. Never writes images/train or labels/train.
// Auto candidates are always reviewed:false.
func ExportSceneMapLearning(samnPath string, opts LearningExportOptions) (LearningExportResult, error) {
	var out LearningExportResult
	if !opts.OptIn {
		return out, ErrLearningExportNotOptedIn
	}
	doc, err := samn.Load(samnPath)
	if err != nil {
		return out, err
	}
	if doc.SceneMap == nil || !doc.SceneMap.HasWindows() {
		return out, fmt.Errorf("generator: %s has no sceneMap windows", samnPath)
	}
	outDir := opts.OutputDir
	if outDir == "" {
		outDir = DefaultSceneMapLearningDir()
	}
	if outDir == "" {
		return out, fmt.Errorf("generator: no learning output directory")
	}
	clipKey := learningClipKey(samnPath, doc.SceneMap)
	clipDir := filepath.Join(outDir, clipKey)
	if err := os.MkdirAll(clipDir, 0o755); err != nil {
		return out, err
	}

	tracePath := filepath.Join(clipDir, "engine_trace.jsonl")
	if err := writeEngineTrace(tracePath, doc.SceneMap); err != nil {
		return out, err
	}
	negPath := filepath.Join(clipDir, "negatives.json")
	nNeg, err := writeNegatives(negPath, doc.SceneMap.Marks)
	if err != nil {
		return out, err
	}
	decPath := filepath.Join(clipDir, "exclude_decisions.jsonl")
	nDec, err := writeExcludeDecisions(decPath, doc.SceneMap.Marks)
	if err != nil {
		return out, err
	}
	autoPath := filepath.Join(clipDir, "auto_candidates.jsonl")
	nAuto, err := writeAutoCandidates(autoPath, doc.SceneMap)
	if err != nil {
		return out, err
	}
	userPath := filepath.Join(clipDir, "user_region_marks.json")
	nUser, err := writeUserRegionMarks(userPath, doc.SceneMap.Marks)
	if err != nil {
		return out, err
	}

	nYoloFrames, nYoloLabels, yoloDir, err := writeReviewedYOLO(samnPath, doc, clipDir)
	if err != nil {
		return out, err
	}

	metaPath := filepath.Join(clipDir, "source.json")
	_ = writeJSON(metaPath, map[string]any{
		"samn":        filepath.Base(samnPath),
		"sha256_head": doc.SceneMap.Video.Sha256Head,
		"grid":        doc.SceneMap.Grid,
		"video":       doc.SceneMap.Video,
		"export":      "scene_map_learning_p5a",
		"note":        "auto_candidates are author=auto reviewed=false — not for YOLO train until reviewed; exclude_decisions feed L1 priors (not profilemodel Style)",
	})

	out = LearningExportResult{
		OutDir:           clipDir,
		Windows:          len(doc.SceneMap.Windows),
		Negatives:        nNeg,
		ExcludeDecisions: nDec,
		AutoCandidates:   nAuto,
		UserRegionMarks:  nUser,
		YoloFrames:       nYoloFrames,
		YoloLabels:       nYoloLabels,
		YoloDir:          yoloDir,
		TracePath:        tracePath,
		NegativesPath:    negPath,
		DecisionsPath:    decPath,
		AutoPath:         autoPath,
		UserRegionsPath:  userPath,
	}
	return out, nil
}

// DeleteSceneMapLearningData removes only scene_map_learning under the
// dataset root (or OutputDir if it ends with that subdir name).
func DeleteSceneMapLearningData(datasetRoot string) error {
	if datasetRoot == "" {
		datasetRoot = DefaultRoiDatasetDir()
	}
	if datasetRoot == "" {
		return fmt.Errorf("generator: no dataset root")
	}
	target := filepath.Join(datasetRoot, SceneMapLearningSubdir)
	if err := os.RemoveAll(target); err != nil {
		return err
	}
	return nil
}

func learningClipKey(samnPath string, m *funscript.SceneMapData) string {
	base := strings.TrimSuffix(filepath.Base(samnPath), filepath.Ext(samnPath))
	if m != nil && m.Video.Sha256Head != "" && len(m.Video.Sha256Head) >= 12 {
		return base + "_" + m.Video.Sha256Head[:12]
	}
	return base
}

func writeEngineTrace(path string, m *funscript.SceneMapData) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	cols, rows := m.Grid.Cols, m.Grid.Rows
	if cols <= 0 {
		cols = 16
	}
	cellW, cellH := 0.0, 0.0
	if m.Video.Width > 0 && cols > 0 {
		cellW = float64(m.Video.Width) / float64(cols)
	}
	if m.Video.Height > 0 && rows > 0 {
		cellH = float64(m.Video.Height) / float64(rows)
	}
	enc := json.NewEncoder(f)
	for _, w := range m.Windows {
		line := map[string]any{
			"startMs":   w.StartMs,
			"endMs":     w.EndMs,
			"tempoHz":   w.TempoHz,
			"chosen":    w.Chosen,
			"signRule":  w.SignRule,
			"trackerR":  w.TrackerR,
			"box":       w.Box,
			"marks":     w.Marks,
			"score_b64": w.ScoreB64,
		}
		if len(w.Box) >= 2 && w.Chosen >= 0 && cellW > 0 && cellH > 0 {
			cx := (float64(w.Chosen%cols) + 0.5) * cellW
			cy := (float64(w.Chosen/cols) + 0.5) * cellH
			line["chosenCellDistPx"] = math.Hypot(cx-w.Box[0], cy-w.Box[1])
		}
		if err := enc.Encode(line); err != nil {
			return err
		}
	}
	return nil
}

func writeNegatives(path string, marks []funscript.SceneMapMark) (int, error) {
	var negs []map[string]any
	for _, m := range marks {
		if strings.ToLower(m.Kind) != "exclude" {
			continue
		}
		entry := map[string]any{
			"id":     m.ID,
			"kind":   m.Kind,
			"rect":   m.Rect,
			"fromMs": m.FromMs,
			"toMs":   m.ToMs,
			"author": m.Author,
			"follow": m.Follow,
		}
		if len(m.Path) > 0 {
			entry["pathLen"] = len(m.Path)
			// Sparse samples for L1/L2 — full path stays in decisions.jsonl.
			step := (len(m.Path) + 7) / 8
			if step < 1 {
				step = 1
			}
			var samples []map[string]any
			for i := 0; i < len(m.Path); i += step {
				p := m.Path[i]
				samples = append(samples, map[string]any{"ms": p.Ms, "rect": p.Rect})
			}
			entry["pathSamples"] = samples
		}
		negs = append(negs, entry)
	}
	if negs == nil {
		negs = []map[string]any{}
	}
	return len(negs), writeJSON(path, map[string]any{
		"version": 1,
		"role":    "not_stroke_source",
		"marks":   negs,
	})
}

// writeExcludeDecisions writes one JSONL line per user exclude mark so L1
// priors / region detectors can learn "knees etc. are not recognition".
// profilemodel Style stays separate (motion-signature → Normal/Soft/Autotune).
func writeExcludeDecisions(path string, marks []funscript.SceneMapMark) (int, error) {
	f, err := os.Create(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	n := 0
	for _, m := range marks {
		if strings.ToLower(m.Kind) != "exclude" {
			continue
		}
		line := map[string]any{
			"version": 1,
			"role":    "exclude_decision",
			"id":      m.ID,
			"kind":    m.Kind,
			"rect":    m.Rect,
			"fromMs":  m.FromMs,
			"toMs":    m.ToMs,
			"author":  m.Author,
			"follow":  m.Follow,
			"path":    m.Path,
		}
		if err := enc.Encode(line); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}

func writeUserRegionMarks(path string, marks []funscript.SceneMapMark) (int, error) {
	var regs []map[string]any
	for _, m := range marks {
		if strings.ToLower(m.Kind) != "region" {
			continue
		}
		if strings.EqualFold(m.Author, "auto") {
			continue
		}
		regs = append(regs, map[string]any{
			"id":     m.ID,
			"kind":   m.Kind,
			"class":  m.Class,
			"role":   m.Role,
			"rect":   m.Rect,
			"fromMs": m.FromMs,
			"toMs":   m.ToMs,
			"author": m.Author,
		})
	}
	if regs == nil {
		regs = []map[string]any{}
	}
	return len(regs), writeJSON(path, map[string]any{"version": 1, "marks": regs})
}

// writeAutoCandidates applies the M5 agreement rule on persisted windows only:
// box centre within one cell of chosen, |trackerR|≥0.3, ≥3 consecutive windows.
// Always reviewed:false — never YOLO train set in P5a.
func writeAutoCandidates(path string, m *funscript.SceneMapData) (int, error) {
	cols, rows := m.Grid.Cols, m.Grid.Rows
	if cols <= 0 {
		cols = 16
	}
	cellW, cellH := 1.0, 1.0
	if m.Video.Width > 0 {
		cellW = float64(m.Video.Width) / float64(cols)
	}
	if m.Video.Height > 0 && rows > 0 {
		cellH = float64(m.Video.Height) / float64(rows)
	}

	agree := make([]bool, len(m.Windows))
	for i, w := range m.Windows {
		if w.Chosen < 0 || len(w.Box) < 2 || math.Abs(w.TrackerR) < 0.3 {
			continue
		}
		cx := (float64(w.Chosen%cols) + 0.5) * cellW
		cy := (float64(w.Chosen/cols) + 0.5) * cellH
		if math.Abs(cx-w.Box[0]) <= cellW && math.Abs(cy-w.Box[1]) <= cellH {
			agree[i] = true
		}
	}

	reviewed := false
	f, err := os.Create(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	n := 0
	run := 0
	for i := 0; i <= len(agree); i++ {
		ok := i < len(agree) && agree[i]
		if ok {
			run++
			continue
		}
		if run >= 3 {
			for j := i - run; j < i; j++ {
				w := m.Windows[j]
				rec := map[string]any{
					"author":     "auto",
					"reviewed":   reviewed,
					"confidence": math.Abs(w.TrackerR),
					"startMs":    w.StartMs,
					"endMs":      w.EndMs,
					"chosen":     w.Chosen,
					"box":        w.Box,
					"class":      "glans",
					"note":       "CSRT box agrees with chosen cell — review before training",
				}
				if err := enc.Encode(rec); err != nil {
					return n, err
				}
				n++
			}
		}
		run = 0
	}
	return n, nil
}

func writeJSON(path string, v any) error {
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}

// writeReviewedYOLO writes a review-gated YOLO detection set beside the L0
// artifacts. It deliberately does not write into the trainer's labels/train
// tree: P5c produces reviewable data; a later explicit training/import step
// decides when it enters a model.
//
// Eligible labels:
//   - region marks authored by a person (author != "auto")
//   - author:auto region marks only when Reviewed is explicitly true
//
// Marks that resolve to the same representative timestamp share one extracted
// frame and one multi-line YOLO label file. This is important for detector
// training: writing duplicate copies of a frame with one box each would make
// the other known objects look like unlabeled background.
//
// AtMs wins; otherwise the midpoint of FromMs/ToMs is used, then FromMs, then 0.
func writeReviewedYOLO(samnPath string, doc *samn.Document, clipDir string) (frames, labels int, yoloDir string, err error) {
	if doc == nil || doc.SceneMap == nil {
		return 0, 0, "", nil
	}
	videoPath := strings.TrimSpace(doc.VideoPath)
	if videoPath == "" {
		return 0, 0, "", nil
	}
	if !filepath.IsAbs(videoPath) {
		videoPath = filepath.Join(filepath.Dir(samnPath), videoPath)
	}
	if _, statErr := os.Stat(videoPath); statErr != nil {
		return 0, 0, "", fmt.Errorf("generator: P5c video for reviewed YOLO export: %w", statErr)
	}

	type sample struct {
		mark funscript.SceneMapMark
		atMs int64
	}
	var samples []sample
	classes := map[string]int{}
	var classNames []string
	for _, m := range reviewedYOLOMarks(doc.SceneMap.Marks) {
		if len(m.Rect) < 4 || strings.TrimSpace(m.Class) == "" {
			continue
		}
		at := int64(0)
		if m.AtMs != nil {
			at = *m.AtMs
		} else if m.ToMs > m.FromMs {
			at = m.FromMs + (m.ToMs-m.FromMs)/2
		} else if m.FromMs > 0 {
			at = m.FromMs
		}
		if at < 0 {
			at = 0
		}
		name := strings.ToLower(strings.TrimSpace(m.Class))
		if _, ok := classes[name]; !ok {
			classes[name] = len(classNames)
			classNames = append(classNames, name)
		}
		samples = append(samples, sample{mark: m, atMs: at})
	}
	if len(samples) == 0 {
		return 0, 0, "", nil
	}

	byTime := make(map[int64][]funscript.SceneMapMark)
	var times []int64
	for _, s := range samples {
		if _, ok := byTime[s.atMs]; !ok {
			times = append(times, s.atMs)
		}
		byTime[s.atMs] = append(byTime[s.atMs], s.mark)
	}
	sort.Slice(times, func(i, j int) bool { return times[i] < times[j] })

	yoloDir = filepath.Join(clipDir, "reviewed_yolo")
	imgDir := filepath.Join(yoloDir, "images")
	lblDir := filepath.Join(yoloDir, "labels")
	if err := os.MkdirAll(imgDir, 0o755); err != nil {
		return 0, 0, "", err
	}
	if err := os.MkdirAll(lblDir, 0o755); err != nil {
		return 0, 0, "", err
	}

	for i, atMs := range times {
		name := fmt.Sprintf("frame_%04d_%d", i, atMs)
		imgPath := filepath.Join(imgDir, name+".png")
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		w, h, dumpErr := DumpFrameAt(ctx, videoPath, imgPath, float64(atMs)/1000.0)
		cancel()
		if dumpErr != nil {
			return frames, labels, yoloDir, dumpErr
		}

		var lines strings.Builder
		frameLabels := 0
		for _, mark := range byTime[atMs] {
			x, y, bw, bh := mark.Rect[0], mark.Rect[1], mark.Rect[2], mark.Rect[3]
			x0, y0 := maxInt(0, x), maxInt(0, y)
			x1, y1 := minInt(w, x+bw), minInt(h, y+bh)
			if x1 <= x0 || y1 <= y0 {
				continue
			}
			classID := classes[strings.ToLower(strings.TrimSpace(mark.Class))]
			xc := (float64(x0+x1) / 2) / float64(w)
			yc := (float64(y0+y1) / 2) / float64(h)
			wn := float64(x1-x0) / float64(w)
			hn := float64(y1-y0) / float64(h)
			fmt.Fprintf(&lines, "%d %.6f %.6f %.6f %.6f\n", classID, xc, yc, wn, hn)
			frameLabels++
		}
		if frameLabels == 0 {
			_ = os.Remove(imgPath)
			continue
		}
		if err := os.WriteFile(filepath.Join(lblDir, name+".txt"), []byte(lines.String()), 0o644); err != nil {
			return frames, labels, yoloDir, err
		}
		frames++
		labels += frameLabels
	}
	if err := writeJSON(filepath.Join(yoloDir, "classes.json"), map[string]any{"version": 1, "names": classNames}); err != nil {
		return frames, labels, yoloDir, err
	}
	return frames, labels, yoloDir, nil
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}
func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func reviewedYOLOMarks(marks []funscript.SceneMapMark) []funscript.SceneMapMark {
	out := make([]funscript.SceneMapMark, 0, len(marks))
	for _, m := range marks {
		if !strings.EqualFold(m.Kind, "region") {
			continue
		}
		if strings.EqualFold(m.Author, "auto") && (m.Reviewed == nil || !*m.Reviewed) {
			continue
		}
		out = append(out, m)
	}
	return out
}
