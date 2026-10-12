package generator

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestBuildSceneMapMetaRoundTrip(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.bin")
	payload := make([]byte, 2048)
	for i := range payload {
		payload[i] = byte(i)
	}
	if err := os.WriteFile(video, payload, 0o644); err != nil {
		t.Fatal(err)
	}
	dto := SceneMapDTO{
		Version: 1,
		Cols:    16,
		Rows:    9,
		Width:   1280,
		Height:  720,
		Windows: []MapWindowDTO{{
			StartMs:    0,
			EndMs:      8000,
			TempoHz:    1.25,
			Score:      []uint8{1, 2, 3, 4},
			ChosenCell: 42,
			BoxCX:      100.5,
			BoxCY:      200.25,
			SignRule:   "continuity",
			TrackerR:   0.05,
			Marks:      []string{"m1"},
		}},
	}
	marks := []SceneMark{{
		ID: "m1", Kind: "exclude",
		Rect:   ROI{X: 10, Y: 20, W: 30, H: 40},
		FromMs: 1000, ToMs: 5000,
	}}
	meta := buildSceneMapMeta(video, 12000, dto, marks)
	if meta == nil || !meta.HasWindows() {
		t.Fatal("expected scene map")
	}
	if meta.Video.Sha256Head == "" {
		t.Fatal("expected sha256_head")
	}
	if meta.Windows[0].Chosen != 42 || meta.Windows[0].ScoreB64 == "" {
		t.Fatalf("window: %+v", meta.Windows[0])
	}
	score, err := funscript.DecodeSceneMapScore(meta.Windows[0].ScoreB64)
	if err != nil || len(score) != 4 || score[3] != 4 {
		t.Fatalf("score: %v %v", score, err)
	}
	if len(meta.Marks) != 1 || meta.Marks[0].Kind != "exclude" {
		t.Fatalf("marks: %+v", meta.Marks)
	}

	// Companion .samn round-trip; .funscript export must omit scene_map.
	fsPath := filepath.Join(dir, "clip.funscript")
	if err := os.WriteFile(fsPath, []byte(`{"actions":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	actions := []funscript.Action{{At: 0, Pos: 20}, {At: 1000, Pos: 80}}
	opts := Options{Profile: "standard"}
	if err := writeCompanionSamn(fsPath, video, actions, opts, nil,
		funscript.ScriptQualityResult{}, nil, meta); err != nil {
		t.Fatal(err)
	}
	doc, err := samn.Load(samn.CompanionSamnPath(fsPath))
	if err != nil {
		t.Fatal(err)
	}
	if doc.SceneMap == nil || doc.SceneMap.Windows[0].Chosen != 42 {
		t.Fatalf("samn sceneMap: %+v", doc.SceneMap)
	}
	outFS := filepath.Join(dir, "export.funscript")
	if err := doc.ExportFunscript(outFS); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(outFS)
	if err != nil {
		t.Fatal(err)
	}
	var parsed map[string]any
	if err := json.Unmarshal(raw, &parsed); err != nil {
		t.Fatal(err)
	}
	metaOut, _ := parsed["metadata"].(map[string]any)
	if _, ok := metaOut["scene_map"]; ok {
		t.Fatal("ExportFunscript must not carry scene_map")
	}
	if _, ok := metaOut["sceneMap"]; ok {
		t.Fatal("ExportFunscript must not carry sceneMap")
	}

	// Old files without sceneMap still load.
	old := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: actions,
	}
	oldPath := filepath.Join(dir, "old.samn")
	if err := samn.Save(oldPath, old); err != nil {
		t.Fatal(err)
	}
	got, err := samn.Load(oldPath)
	if err != nil {
		t.Fatal(err)
	}
	if got.SceneMap != nil {
		t.Fatal("old file should have nil sceneMap")
	}
}

func TestBuildSceneMapMetaNilWithoutWindows(t *testing.T) {
	if buildSceneMapMeta("", 0, SceneMapDTO{Version: 1}, nil) != nil {
		t.Fatal("empty windows must yield nil")
	}
}
