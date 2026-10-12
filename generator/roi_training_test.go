package generator

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBuildBootstrapArgsSingleRegion(t *testing.T) {
	regions := []RoiTrainingRegion{{ROI: ROI{X: 1, Y: 2, W: 3, H: 4}, ClassName: "brust"}}
	args := buildBootstrapArgs("script.py", "v.mp4", regions, "/data", "")
	joined := strings.Join(args, " ")
	// Legacy DE "brust" normalizes to English "breasts".
	for _, want := range []string{"--video v.mp4", "--roi 1,2,3,4", "--class-name breasts", "--output-dir /data"} {
		if !strings.Contains(joined, want) {
			t.Errorf("Argument %q fehlt in: %s", want, joined)
		}
	}
	for _, unwanted := range []string{"--roi2", "--class-name2", "--sample-prefix"} {
		if strings.Contains(joined, unwanted) {
			t.Errorf("unerwartetes Argument %q ohne 2. Region/Präfix: %s", unwanted, joined)
		}
	}
}

func TestBuildBootstrapArgsNineRegions(t *testing.T) {
	regions := []RoiTrainingRegion{
		{ROI: ROI{1, 1, 10, 10}, ClassName: "face"},
		{ROI: ROI{2, 2, 10, 10}, ClassName: "mouth"},
		{ROI: ROI{3, 3, 10, 10}, ClassName: "breasts"},
		{ROI: ROI{4, 4, 10, 10}, ClassName: "nipples"},
		{ROI: ROI{5, 5, 10, 10}, ClassName: "hand_1"},
		{ROI: ROI{6, 6, 10, 10}, ClassName: "hand_2"},
		{ROI: ROI{7, 7, 10, 10}, ClassName: "penis"},
		{ROI: ROI{8, 8, 10, 10}, ClassName: "glans"},
		{ROI: ROI{9, 9, 10, 10}, ClassName: "vagina"},
	}
	args := buildBootstrapArgs("script.py", "v.mp4", regions, "/data", "pfx")
	joined := strings.Join(args, " ")
	for _, want := range []string{"--roi9", "--class-name9 vagina", "--sample-prefix pfx"} {
		if !strings.Contains(joined, want) {
			t.Errorf("missing %q in %s", want, joined)
		}
	}
}

func TestBuildBootstrapArgsTwelveFreeTagsAllClasses(t *testing.T) {
	// Free tags: full taxonomy + nipples×2 + breasts must all reach Python —
	// regression for the old i<9 cap that dropped later boxes (looked glans-only).
	regions := []RoiTrainingRegion{
		{ROI: ROI{1, 1, 10, 10}, ClassName: "face"},
		{ROI: ROI{2, 2, 10, 10}, ClassName: "mouth"},
		{ROI: ROI{3, 3, 10, 10}, ClassName: "breasts"},
		{ROI: ROI{4, 4, 10, 10}, ClassName: "nipples"},
		{ROI: ROI{5, 5, 10, 10}, ClassName: "nipples"},
		{ROI: ROI{6, 6, 10, 10}, ClassName: "hand_1"},
		{ROI: ROI{7, 7, 10, 10}, ClassName: "hand_2"},
		{ROI: ROI{8, 8, 10, 10}, ClassName: "penis"},
		{ROI: ROI{9, 9, 10, 10}, ClassName: "glans"},
		{ROI: ROI{10, 10, 10, 10}, ClassName: "vagina"},
		{ROI: ROI{11, 11, 10, 10}, ClassName: "breasts"},
		{ROI: ROI{12, 12, 10, 10}, ClassName: "mouth"},
	}
	args := buildBootstrapArgs("script.py", "v.mp4", regions, "/data", "pfx")
	joined := strings.Join(args, " ")
	for _, want := range []string{
		"--class-name face",
		"--class-name2 mouth",
		"--class-name3 breasts",
		"--class-name4 nipples",
		"--class-name5 nipples",
		"--roi12", "--class-name12 mouth",
	} {
		if !strings.Contains(joined, want) {
			t.Errorf("missing %q in %s", want, joined)
		}
	}
}

func TestBuildBootstrapArgsStartSeconds(t *testing.T) {
	regions := []RoiTrainingRegion{{ROI: ROI{X: 1, Y: 2, W: 3, H: 4}, ClassName: "brust"}}
	args := buildBootstrapArgsOpts("script.py", "v.mp4", regions, "/data", "", 12, 4.5, 1.0)
	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "--start-seconds 4.500") {
		t.Errorf("start-seconds fehlt in: %s", joined)
	}
	args0 := buildBootstrapArgsOpts("script.py", "v.mp4", regions, "/data", "", 12, 0, 1.0)
	if strings.Contains(strings.Join(args0, " "), "--start-seconds") {
		t.Errorf("start-seconds bei 0 unerwartet: %v", args0)
	}
	argsScale := buildBootstrapArgsOpts("script.py", "v.mp4", regions, "/data", "", 12, 0, 1.15)
	if !strings.Contains(strings.Join(argsScale, " "), "--box-scale 1.150") {
		t.Errorf("box-scale fehlt in: %v", argsScale)
	}
	argsDefaultScale := buildBootstrapArgsOpts("script.py", "v.mp4", regions, "/data", "", 12, 0, 1.0)
	if strings.Contains(strings.Join(argsDefaultScale, " "), "--box-scale") {
		t.Errorf("box-scale bei 1.0 unerwartet: %v", argsDefaultScale)
	}
}

func TestBuildTrainArgsDefaults(t *testing.T) {
	// epochs<=0 und device="" sollen kein Flag erzeugen - train_yolo_model.py
	// hat dafür eigene Standardwerte (100 Epochen, device="auto"), die dann
	// nicht durch ein leeres/nullwertiges Flag überschrieben werden sollen.
	args := buildTrainArgs("script.py", "/data", "/models/roi.onnx", 0, "")
	joined := strings.Join(args, " ")
	for _, want := range []string{"--dataset-dir /data", "--output /models/roi.onnx"} {
		if !strings.Contains(joined, want) {
			t.Errorf("Argument %q fehlt in: %s", want, joined)
		}
	}
	for _, unwanted := range []string{"--epochs", "--device"} {
		if strings.Contains(joined, unwanted) {
			t.Errorf("unerwartetes Argument %q ohne explizite Angabe: %s", unwanted, joined)
		}
	}
}

func TestBuildTrainArgsExplicitValues(t *testing.T) {
	args := buildTrainArgs("script.py", "/data", "/models/roi.onnx", 50, "cpu")
	joined := strings.Join(args, " ")
	for _, want := range []string{"--epochs 50", "--device cpu"} {
		if !strings.Contains(joined, want) {
			t.Errorf("Argument %q fehlt in: %s", want, joined)
		}
	}
}

func TestRoiArgFormatsAsCommaSeparated(t *testing.T) {
	got := roiArg(ROI{X: 10, Y: 20, W: 30, H: 40})
	if got != "10,20,30,40" {
		t.Fatalf("erwartete '10,20,30,40', bekam %q", got)
	}
}

func TestDefaultRoiDatasetDirIsUnderSamNPlayerNamespace(t *testing.T) {
	got := DefaultRoiDatasetDir()
	if got == "" {
		t.Skip("os.UserConfigDir() liefert in dieser Umgebung keinen Pfad")
	}
	if !strings.Contains(got, "SamNPlayer") || !strings.HasSuffix(got, "roi_training_dataset") {
		t.Fatalf("erwarte einen Pfad unter .../SamNPlayer/roi_training_dataset, bekam %q", got)
	}
}

func TestRunRoiModelTrainingFailsFastWithoutDataYAML(t *testing.T) {
	dir := t.TempDir()
	err := RunRoiModelTraining(dir, filepath.Join(dir, "out.onnx"), 1, "cpu", nil)
	if err == nil {
		t.Fatal("expected error when data.yaml is missing")
	}
	msg := err.Error()
	if !strings.Contains(msg, "data.yaml") {
		t.Fatalf("error should mention data.yaml, got: %v", err)
	}
	if !strings.Contains(msg, "Use for training") {
		t.Fatalf("error should point user to Use for training, got: %v", err)
	}
}

func TestRunRoiModelTrainingFailsFastOnEmptyDataset(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "data.yaml"), []byte("train: images/train\nval: images/val\nnames:\n  0: x\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	err := RunRoiModelTraining(dir, filepath.Join(dir, "out.onnx"), 1, "cpu", nil)
	if err == nil {
		t.Fatal("expected error when dataset has no images")
	}
	msg := err.Error()
	if !strings.Contains(msg, "empty") && !strings.Contains(msg, "labeled") {
		t.Fatalf("error should mention empty/labeled dataset, got: %v", err)
	}
}

func TestBuildTrainArgsClampsEpochs(t *testing.T) {
	args := buildTrainArgs("script.py", "/data", "/models/roi.onnx", 10000, "")
	got := strings.Join(args, " ")
	if !strings.Contains(got, "--epochs 500") {
		t.Fatalf("epochs must be capped at %d: %s", MaxRoiTrainingEpochs, got)
	}
	if ClampRoiTrainingEpochs(80) != 80 || ClampRoiTrainingEpochs(0) != 0 {
		t.Fatal("values inside the range must stay unchanged")
	}
}
