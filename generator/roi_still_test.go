package generator

import (
	"image"
	"image/color"
	"image/jpeg"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestStillTrainingGetsValSplit(t *testing.T) {
	dir := t.TempDir()
	imgPath := filepath.Join(dir, "src.jpg")
	if err := writeTestJPEG(imgPath, 64, 64); err != nil {
		t.Fatal(err)
	}
	regions := []RoiTrainingRegion{{
		ROI:       ROI{X: 10, Y: 10, W: 20, H: 20},
		ClassName: "hand",
	}}
	out := filepath.Join(dir, "ds")
	if _, err := AddStillTrainingSample(imgPath, regions, out, "a"); err != nil {
		t.Fatal(err)
	}
	if countImages(filepath.Join(out, "images", "train")) != 1 {
		t.Fatalf("first still should be train")
	}
	if _, err := AddStillTrainingSample(imgPath, regions, out, "b"); err != nil {
		t.Fatal(err)
	}
	if countImages(filepath.Join(out, "images", "val")) < 1 {
		t.Fatalf("second still should land in val when train already has one")
	}
}

func TestEnsureRoiDatasetSplitsHealsEmptyVal(t *testing.T) {
	dir := t.TempDir()
	imgTrain := filepath.Join(dir, "images", "train")
	lblTrain := filepath.Join(dir, "labels", "train")
	if err := os.MkdirAll(imgTrain, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(lblTrain, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "images", "val"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := writeTestJPEG(filepath.Join(imgTrain, "solo.jpg"), 32, 32); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(lblTrain, "solo.txt"), []byte("0 0.5 0.5 0.2 0.2\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	trainN, valN, err := EnsureRoiDatasetSplits(dir)
	if err != nil {
		t.Fatal(err)
	}
	if trainN < 1 || valN < 1 {
		t.Fatalf("expected both splits ≥1 after heal, got train=%d val=%d", trainN, valN)
	}
	if _, err := os.Stat(filepath.Join(dir, "images", "val", "solo.jpg")); err != nil {
		t.Fatalf("val image missing: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "labels", "val", "solo.txt")); err != nil {
		t.Fatalf("val label missing: %v", err)
	}
}

func TestPrepareRoiDatasetForTrainingEmpty(t *testing.T) {
	dir := t.TempDir()
	_ = os.WriteFile(filepath.Join(dir, "data.yaml"), []byte("train: images/train\nval: images/val\n"), 0o644)
	err := PrepareRoiDatasetForTraining(dir)
	if err == nil {
		t.Fatal("expected error for empty dataset")
	}
	if !strings.Contains(err.Error(), "empty") && !strings.Contains(err.Error(), "labeled") {
		t.Fatalf("error should mention empty/labeled, got: %v", err)
	}
	if !strings.Contains(err.Error(), "Use for training") {
		t.Fatalf("error should point to Use for training, got: %v", err)
	}
}

func TestPrepareRoiDatasetForTrainingHealsThenOK(t *testing.T) {
	dir := t.TempDir()
	imgTrain := filepath.Join(dir, "images", "train")
	if err := os.MkdirAll(imgTrain, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "labels", "train"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := writeTestJPEG(filepath.Join(imgTrain, "a.jpg"), 32, 32); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "labels", "train", "a.txt"), []byte("0 0.5 0.5 0.1 0.1\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := PrepareRoiDatasetForTraining(dir); err != nil {
		t.Fatalf("train-only dataset should heal: %v", err)
	}
	if countImages(filepath.Join(dir, "images", "val")) < 1 {
		t.Fatal("val should be non-empty after PrepareRoiDatasetForTraining")
	}
}

func writeTestJPEG(path string, w, h int) error {
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.Set(x, y, color.RGBA{R: 40, G: 80, B: 120, A: 255})
		}
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return jpeg.Encode(f, img, &jpeg.Options{Quality: 80})
}
