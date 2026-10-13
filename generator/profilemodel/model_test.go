package profilemodel

import (
	"os"
	"path/filepath"
	"sync"
	"testing"
)

func signature(offset float64) map[string]float64 {
	out := make(map[string]float64, len(SignatureFields))
	for i, field := range SignatureFields {
		out[field] = float64(i)/20 + offset
	}
	return out
}

func TestAppendTrainLoadAndSuggest(t *testing.T) {
	dir := t.TempDir()
	labels := filepath.Join(dir, "scenes.jsonl")
	modelPath := filepath.Join(dir, "model.json")
	if err := AppendLabel(labels, "calm one", "standard", signature(0)); err != nil {
		t.Fatal(err)
	}
	if err := AppendLabel(labels, "calm two", "standard", signature(0.02)); err != nil {
		t.Fatal(err)
	}
	if err := AppendLabel(labels, "soft one", "weich", signature(0.35)); err != nil {
		t.Fatal(err)
	}
	if err := AppendLabel(labels, "soft two", "weich", signature(0.37)); err != nil {
		t.Fatal(err)
	}

	status, err := StatusAt(labels, modelPath)
	if err != nil {
		t.Fatal(err)
	}
	if status.LabelledScenes != 4 || status.UsableSamples != 4 || !status.ReadyToTrain {
		t.Fatalf("unexpected pre-train status: %+v", status)
	}
	model, err := Train(labels, modelPath)
	if err != nil {
		t.Fatal(err)
	}
	if model.SampleCount != 4 || len(model.Classes) != 2 {
		t.Fatalf("unexpected model: %+v", model)
	}
	if _, err := os.Stat(modelPath); err != nil {
		t.Fatalf("model was not persisted: %v", err)
	}
	loaded, err := Load(modelPath)
	if err != nil {
		t.Fatal(err)
	}
	got := Suggest(signature(0.01), loaded)
	if !got.Found || got.Profile != "standard" || got.Confidence <= 0.5 {
		t.Fatalf("unexpected suggestion: %+v", got)
	}
	status, err = StatusAt(labels, modelPath)
	if err != nil {
		t.Fatal(err)
	}
	if !status.ModelAvailable || status.TrainedSamples != 4 || status.TrainedAt == "" {
		t.Fatalf("unexpected post-train status: %+v", status)
	}
}

func TestSuggestRejectsUnknownAndAmbiguousSignatures(t *testing.T) {
	model := Model{
		Version: ModelVersion, SignatureVersion: SignatureVersion,
		SampleCount: 4,
		Classes: []ProfileClass{
			{Profile: "standard", Count: 2, Centroid: signature(0), Radius: 0.01},
			{Profile: "weich", Count: 2, Centroid: signature(0.04), Radius: 0.01},
		},
	}
	if got := Suggest(signature(0.02), model); got.Found {
		t.Fatalf("ambiguous scene must not be classified: %+v", got)
	}
	if got := Suggest(signature(0.8), model); got.Found {
		t.Fatalf("unknown scene must not be classified: %+v", got)
	}
}

func TestOldProfileLabelsRemainTrainable(t *testing.T) {
	dir := t.TempDir()
	labels := filepath.Join(dir, "old.jsonl")
	line := `{"version":1,"label":"autotune","signature":{"vertical_share":0.1,"motion_center_y":0.2,"motion_center_x":0.3,"motion_spread":0.4,"region_count":0.5,"camera_motion":0.6,"stroke_symmetry":0.7,"rhythm_strength":0.8},"parameters":{}}` + "\n"
	if err := os.WriteFile(labels, []byte(line+line), 0o644); err != nil {
		t.Fatal(err)
	}
	status, err := StatusAt(labels, filepath.Join(dir, "missing.json"))
	if err != nil {
		t.Fatal(err)
	}
	if status.UsableSamples != 2 || !status.ReadyToTrain {
		t.Fatalf("legacy labels were not recognized: %+v", status)
	}
}

func TestAppendLabelRejectsInvalidInput(t *testing.T) {
	path := filepath.Join(t.TempDir(), "scenes.jsonl")
	if err := AppendLabel(path, "", "standard", signature(0)); err == nil {
		t.Fatal("expected empty scene name to fail")
	}
	if err := AppendLabel(path, "scene", "mystery", signature(0)); err == nil {
		t.Fatal("expected unsupported profile to fail")
	}
	bad := signature(0)
	delete(bad, SignatureFields[0])
	if err := AppendLabel(path, "scene", "standard", bad); err == nil {
		t.Fatal("expected incomplete signature to fail")
	}
}

func TestCorruptModelDoesNotBlockRetraining(t *testing.T) {
	dir := t.TempDir()
	labels := filepath.Join(dir, "scenes.jsonl")
	modelPath := filepath.Join(dir, "model.json")
	for i := 0; i < 2; i++ {
		if err := AppendLabel(labels, "scene", "standard", signature(float64(i)/100)); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(modelPath, []byte("{broken"), 0o644); err != nil {
		t.Fatal(err)
	}
	status, err := StatusAt(labels, modelPath)
	if err != nil {
		t.Fatalf("corrupt derived model should be reported, not block recovery: %v", err)
	}
	if !status.ReadyToTrain || status.ModelWarning == "" || status.ModelAvailable {
		t.Fatalf("unexpected recoverable status: %+v", status)
	}
	if _, err := Train(labels, modelPath); err != nil {
		t.Fatalf("retraining should replace corrupt model: %v", err)
	}
	status, err = StatusAt(labels, modelPath)
	if err != nil || !status.ModelAvailable || status.ModelWarning != "" {
		t.Fatalf("model was not repaired: status=%+v err=%v", status, err)
	}
}

func TestAppendLabelSerializesConcurrentWriters(t *testing.T) {
	path := filepath.Join(t.TempDir(), "scenes.jsonl")
	const count = 20
	var wg sync.WaitGroup
	errs := make(chan error, count)
	for i := 0; i < count; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			errs <- AppendLabel(path, "scene", "standard", signature(float64(i)/1000))
		}(i)
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		if err != nil {
			t.Fatal(err)
		}
	}
	status, err := StatusAt(path, filepath.Join(t.TempDir(), "missing.json"))
	if err != nil {
		t.Fatal(err)
	}
	if status.LabelledScenes != count || status.UsableSamples != count {
		t.Fatalf("concurrent labels were lost/corrupted: %+v", status)
	}
}
