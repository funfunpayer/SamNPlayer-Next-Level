package generator

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestWriteCompanionSamnStrokeContactFeel(t *testing.T) {
	dir := t.TempDir()
	fsPath := filepath.Join(dir, "clip.funscript")
	if err := os.WriteFile(fsPath, []byte(`{"actions":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	actions := []funscript.Action{{At: 0, Pos: 20}, {At: 1000, Pos: 80}}
	opts := Options{
		Profile:              "standard",
		ContactVibration:     true,
		ContactVibrationSpan: 0.75,
		RegionClass:          "glans",
		TipROI:               ROI{X: 10, Y: 20, W: 40, H: 50},
		ROI2:                 ROI{X: 100, Y: 80, W: 30, H: 30},
		RegionClass2:         "nipples",
	}
	traj := &funscript.TrajectoryData{
		Width:  1280,
		Height: 720,
		Tip:    []funscript.TrajectoryPoint{{AtMs: 0, X: 30, Y: 40}, {AtMs: 1000, X: 35, Y: 42}},
	}
	quality := funscript.ScriptQualityResult{Score: 0.8, Passed: true}
	if err := writeCompanionSamn(fsPath, "", actions, opts, nil, quality, traj, nil); err != nil {
		t.Fatal(err)
	}
	doc, err := samn.Load(samn.CompanionSamnPath(fsPath))
	if err != nil {
		t.Fatal(err)
	}
	if !doc.Recipe.ContactVibration {
		t.Fatalf("stroke companion .samn missing contact_vibration recipe: %+v", doc.Recipe)
	}
	if doc.ContactMarks == nil || doc.ContactMarks.Tip == nil || doc.ContactMarks.Primary == nil {
		t.Fatalf("contact marks not on .samn: %+v", doc.ContactMarks)
	}
	if doc.Trajectory == nil || len(doc.Trajectory.Tip) != 2 {
		t.Fatalf("trajectory not on .samn: %+v", doc.Trajectory)
	}
	// Round-trip into player Script must keep feel Stage A inputs.
	script, err := doc.ToFunscript()
	if err != nil {
		t.Fatal(err)
	}
	if script.Metadata.DeviceRecipe == nil || !script.Metadata.DeviceRecipe.ContactVibration {
		t.Fatal("ToFunscript dropped contact recipe")
	}
	if script.Metadata.ContactMarks == nil || script.Metadata.Trajectory == nil {
		t.Fatal("ToFunscript dropped marks/trajectory")
	}
	// Improve export must not wipe feel metadata on companion .funscript.
	outFS := filepath.Join(dir, "clip_export.funscript")
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
	meta, _ := parsed["metadata"].(map[string]any)
	if meta["contact_marks"] == nil {
		t.Fatal("ExportFunscript dropped contact_marks")
	}
	if meta["trajectory"] == nil {
		t.Fatal("ExportFunscript dropped trajectory")
	}
	if meta["device_recipe"] == nil {
		t.Fatal("ExportFunscript dropped device_recipe")
	}
}
