package generator

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestSceneMapFromPersistRoundTrip(t *testing.T) {
	dto := SceneMapDTO{
		Version: 1,
		Cols:    16,
		Rows:    9,
		Width:   640,
		Height:  360,
		Windows: []MapWindowDTO{{
			StartMs:    0,
			EndMs:      8000,
			TempoHz:    1.5,
			Score:      []uint8{9, 8, 7},
			ChosenCell: 7,
			BoxCX:      11,
			BoxCY:      22,
			SignRule:   "continuity",
			Marks:      []string{"m1"},
		}},
	}
	marks := []SceneMark{{
		ID: "m1", Kind: "exclude",
		Rect:   ROI{X: 1, Y: 2, W: 3, H: 4},
		FromMs: 0, ToMs: 8000,
	}}
	meta := buildSceneMapMeta("", 9000, dto, marks)
	got, gotMarks, err := SceneMapFromPersist(meta)
	if err != nil {
		t.Fatal(err)
	}
	if got.Cols != 16 || got.Rows != 9 || len(got.Windows) != 1 {
		t.Fatalf("map: %+v", got)
	}
	w := got.Windows[0]
	if w.ChosenCell != 7 || w.BoxCX != 11 || len(w.Score) != 3 || w.Score[0] != 9 {
		t.Fatalf("window: %+v", w)
	}
	if len(gotMarks) != 1 || gotMarks[0].Kind != "exclude" || gotMarks[0].Rect.W != 3 {
		t.Fatalf("marks: %+v", gotMarks)
	}
}

func TestLoadSceneMapBesideVideo(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	// No companion → not found, no error.
	empty, err := LoadSceneMapBesideVideo(video)
	if err != nil || empty.Found {
		t.Fatalf("want empty: %+v %v", empty, err)
	}

	meta := buildSceneMapMeta(video, 5000, SceneMapDTO{
		Version: 1, Cols: 4, Rows: 2, Width: 100, Height: 50,
		Windows: []MapWindowDTO{{
			StartMs: 0, EndMs: 4000, Score: []uint8{1, 2}, ChosenCell: -1,
		}},
	}, []SceneMark{{
		ID: "m9", Kind: "source",
		Rect: ROI{X: 5, Y: 6, W: 7, H: 8}, FromMs: 0, ToMs: 4000,
	}})
	actions := []funscript.Action{{At: 0, Pos: 10}, {At: 1000, Pos: 90}}
	fsPath := filepath.Join(dir, "clip.funscript")
	if err := os.WriteFile(fsPath, []byte(`{"actions":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := writeCompanionSamn(fsPath, video, actions, Options{}, nil,
		funscript.ScriptQualityResult{}, nil, meta); err != nil {
		t.Fatal(err)
	}

	got, err := LoadSceneMapBesideVideo(video)
	if err != nil {
		t.Fatal(err)
	}
	if !got.Found || len(got.Marks) != 1 || got.Marks[0].ID != "m9" {
		t.Fatalf("load: %+v", got)
	}
	if len(got.Map.Windows) != 1 || got.Map.Windows[0].Score[1] != 2 {
		t.Fatalf("windows: %+v", got.Map)
	}
	if got.Path != samn.CompanionSamnPath(video) {
		t.Fatalf("path: %q", got.Path)
	}
}

func TestSceneMarkReviewedConfidenceRoundTrip(t *testing.T) {
	// P5c follow-up: funscript.SceneMapMark already has Reviewed/Confidence;
	// generator persist/load must not drop them on GUI companion round-trip.
	reviewed := true
	marks := []SceneMark{{
		ID: "auto1", Kind: "region",
		Rect:   ROI{X: 10, Y: 20, W: 30, H: 40},
		FromMs: 0, ToMs: 8000,
		Class:      "tip",
		Author:     "auto",
		Confidence: 0.91,
		Reviewed:   &reviewed,
	}}
	meta := buildSceneMapMeta("", 9000, SceneMapDTO{
		Version: 1, Cols: 4, Rows: 2, Width: 100, Height: 50,
		Windows: []MapWindowDTO{{
			StartMs: 0, EndMs: 8000, Score: []uint8{1}, ChosenCell: 0,
		}},
	}, marks)
	if meta == nil || len(meta.Marks) != 1 {
		t.Fatalf("persist marks: %+v", meta)
	}
	pm := meta.Marks[0]
	if pm.Confidence != 0.91 || pm.Reviewed == nil || !*pm.Reviewed || pm.Author != "auto" {
		t.Fatalf("persisted mark lost review fields: %+v", pm)
	}
	_, got, err := SceneMapFromPersist(meta)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 {
		t.Fatalf("load marks: %+v", got)
	}
	g := got[0]
	if g.Confidence != 0.91 || g.Reviewed == nil || !*g.Reviewed || g.Author != "auto" || g.Class != "tip" {
		t.Fatalf("loaded mark lost review fields: %+v", g)
	}
	// Explicit false must also survive (unreviewed auto must stay false).
	unrev := false
	marks[0].Reviewed = &unrev
	marks[0].Confidence = 0.4
	meta2 := buildSceneMapMeta("", 9000, SceneMapDTO{
		Version: 1, Cols: 4, Rows: 2, Width: 100, Height: 50,
		Windows: []MapWindowDTO{{
			StartMs: 0, EndMs: 8000, Score: []uint8{1}, ChosenCell: 0,
		}},
	}, marks)
	_, got2, err := SceneMapFromPersist(meta2)
	if err != nil {
		t.Fatal(err)
	}
	if got2[0].Reviewed == nil || *got2[0].Reviewed || got2[0].Confidence != 0.4 {
		t.Fatalf("false reviewed must round-trip: %+v", got2[0])
	}
}

func TestLoadSceneMapBesideVideoOldSamn(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "old.mp4")
	_ = os.WriteFile(video, []byte("x"), 0o644)
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []funscript.Action{{At: 0, Pos: 0}, {At: 100, Pos: 100}},
	}
	if err := samn.Save(samn.CompanionSamnPath(video), doc); err != nil {
		t.Fatal(err)
	}
	got, err := LoadSceneMapBesideVideo(video)
	if err != nil || got.Found {
		t.Fatalf("old file should not found: %+v %v", got, err)
	}
}
