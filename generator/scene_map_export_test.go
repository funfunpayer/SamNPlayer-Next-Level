package generator

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestExportSceneMapLearningRequiresOptIn(t *testing.T) {
	dir := t.TempDir()
	path := writeLearningFixture(t, dir, 5, true)
	_, err := ExportSceneMapLearning(path, LearningExportOptions{OptIn: false, OutputDir: filepath.Join(dir, "out")})
	if err != ErrLearningExportNotOptedIn {
		t.Fatalf("want ErrLearningExportNotOptedIn, got %v", err)
	}
}

func TestExportSceneMapLearningArtifacts(t *testing.T) {
	dir := t.TempDir()
	path := writeLearningFixture(t, dir, 5, true)
	outRoot := filepath.Join(dir, "scene_map_learning")
	res, err := ExportSceneMapLearning(path, LearningExportOptions{OptIn: true, OutputDir: outRoot})
	if err != nil {
		t.Fatal(err)
	}
	if res.Windows != 5 {
		t.Fatalf("windows %d", res.Windows)
	}
	if res.Negatives != 1 {
		t.Fatalf("negatives %d", res.Negatives)
	}
	if res.ExcludeDecisions != 1 {
		t.Fatalf("exclude decisions %d", res.ExcludeDecisions)
	}
	if _, err := os.Stat(res.DecisionsPath); err != nil {
		t.Fatalf("decisions path: %v", err)
	}
	if res.AutoCandidates < 3 {
		t.Fatalf("auto candidates %d, want ≥3", res.AutoCandidates)
	}
	if res.UserRegionMarks != 1 {
		t.Fatalf("user regions %d", res.UserRegionMarks)
	}
	// No YOLO train pollution
	if _, err := os.Stat(filepath.Join(outRoot, "labels", "train")); !os.IsNotExist(err) {
		t.Fatal("must not write labels/train")
	}
	f, err := os.Open(res.AutoPath)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		var rec map[string]any
		if err := json.Unmarshal(sc.Bytes(), &rec); err != nil {
			t.Fatal(err)
		}
		if rec["author"] != "auto" {
			t.Fatalf("author %v", rec["author"])
		}
		if rec["reviewed"] != false {
			t.Fatalf("reviewed must be false, got %v", rec["reviewed"])
		}
	}
}

func TestExportSceneMapLearningNoAgreementNoAuto(t *testing.T) {
	dir := t.TempDir()
	path := writeLearningFixture(t, dir, 5, false)
	outRoot := filepath.Join(dir, "out")
	res, err := ExportSceneMapLearning(path, LearningExportOptions{OptIn: true, OutputDir: outRoot})
	if err != nil {
		t.Fatal(err)
	}
	if res.AutoCandidates != 0 {
		t.Fatalf("auto %d want 0", res.AutoCandidates)
	}
}

func TestReviewedYOLOEligibilityGate(t *testing.T) {
	reviewed := true
	unreviewed := false
	marks := []funscript.SceneMapMark{
		{ID: "user", Kind: "region", Class: "glans", Rect: []int{10, 20, 30, 40}, Author: "user"},
		{ID: "auto-no-flag", Kind: "region", Class: "penis", Rect: []int{10, 20, 30, 40}, Author: "auto"},
		{ID: "auto-false", Kind: "region", Class: "penis", Rect: []int{10, 20, 30, 40}, Author: "auto", Reviewed: &unreviewed},
		{ID: "auto-true", Kind: "region", Class: "penis", Rect: []int{10, 20, 30, 40}, Author: "auto", Reviewed: &reviewed},
		{ID: "exclude", Kind: "exclude", Class: "face", Rect: []int{10, 20, 30, 40}, Author: "user"},
	}
	got := reviewedYOLOMarks(marks)
	if len(got) != 2 {
		t.Fatalf("eligible marks = %d, want 2", len(got))
	}
	if got[0].ID != "user" || got[1].ID != "auto-true" {
		t.Fatalf("eligible IDs = %q, %q", got[0].ID, got[1].ID)
	}
}

func TestReviewedYOLOGroupsMarksByTimestamp(t *testing.T) {
	at := int64(1234)
	reviewed := true
	marks := []funscript.SceneMapMark{
		{ID: "user-a", Kind: "region", Class: "glans", Rect: []int{10, 20, 30, 40}, Author: "user", AtMs: &at},
		{ID: "auto-b", Kind: "region", Class: "contact", Rect: []int{50, 60, 20, 20}, Author: "auto", Reviewed: &reviewed, AtMs: &at},
	}
	got := reviewedYOLOMarks(marks)
	if len(got) != 2 {
		t.Fatalf("eligible marks = %d, want 2", len(got))
	}

	// The exporter groups by the representative timestamp before extracting
	// frames. Keep this regression assertion independent of ffmpeg.
	counts := map[int64]int{}
	for _, m := range got {
		var ts int64
		if m.AtMs != nil {
			ts = *m.AtMs
		}
		counts[ts]++
	}
	if len(counts) != 1 || counts[at] != 2 {
		t.Fatalf("timestamp groups = %#v, want one frame with two labels", counts)
	}
}

func TestDeleteSceneMapLearningDataPreservesSibling(t *testing.T) {
	root := t.TempDir()
	learn := filepath.Join(root, SceneMapLearningSubdir, "clip_x")
	if err := os.MkdirAll(learn, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(learn, "engine_trace.jsonl"), []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	hand := filepath.Join(root, "labels", "train", "keep.txt")
	if err := os.MkdirAll(filepath.Dir(hand), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(hand, []byte("hand"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := DeleteSceneMapLearningData(root); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, SceneMapLearningSubdir)); !os.IsNotExist(err) {
		t.Fatal("learning dir should be gone")
	}
	if _, err := os.Stat(hand); err != nil {
		t.Fatal("hand YOLO sample must remain")
	}
}

func writeLearningFixture(t *testing.T, dir string, nWin int, agree bool) string {
	t.Helper()
	const cols, rows = 16, 9
	w, h := 1280, 720
	cellW := float64(w) / float64(cols)
	cellH := float64(h) / float64(rows)
	chosen := 5*cols + 8
	cx := (float64(chosen%cols) + 0.5) * cellW
	cy := (float64(chosen/cols) + 0.5) * cellH
	if !agree {
		cx, cy = 40, 40 // far from chosen cell
	}
	score := make([]uint8, cols*rows)
	score[chosen] = 255
	wins := make([]funscript.SceneMapWindow, nWin)
	for i := range wins {
		wins[i] = funscript.SceneMapWindow{
			StartMs:  int64(i * 2000),
			EndMs:    int64(i*2000 + 8000),
			TempoHz:  1.0,
			ScoreB64: funscript.EncodeSceneMapScore(score),
			Chosen:   chosen,
			SignRule: "tracker",
			TrackerR: 0.55,
			Box:      []float64{cx, cy},
			Marks:    []string{"m1"},
		}
	}
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []samn.Point{{At: 0, Pos: 50}, {At: 1000, Pos: 60}},
		SceneMap: &funscript.SceneMapData{
			Version: 1,
			Video:   funscript.SceneMapVideo{DurationMs: 20000, Width: w, Height: h, Sha256Head: "abcdef0123456789"},
			Grid:    funscript.SceneMapGrid{Cols: cols, Rows: rows},
			Windows: wins,
			Marks: []funscript.SceneMapMark{
				{ID: "m1", Kind: "exclude", Rect: []int{10, 500, 100, 80}, FromMs: 0, ToMs: 20000, Author: "user", Follow: true,
					Path: []funscript.SceneMapMarkSample{{Ms: 0, Rect: []int{10, 500, 100, 80}}, {Ms: 10000, Rect: []int{40, 520, 100, 80}}}},
				{ID: "m2", Kind: "region", Class: "glans", Rect: []int{600, 400, 80, 90}, Author: "user"},
			},
		},
	}
	path := filepath.Join(dir, "clip.samn")
	if err := samn.Save(path, doc); err != nil {
		t.Fatal(err)
	}
	return path
}
