//go:build suite

package main

import (
	"encoding/base64"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeRoiSample(t *testing.T, datasetDir, split, name, labelBody string) string {
	t.Helper()
	imgDir := filepath.Join(datasetDir, "images", split)
	lblDir := filepath.Join(datasetDir, "labels", split)
	if err := os.MkdirAll(imgDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(lblDir, 0o755); err != nil {
		t.Fatal(err)
	}
	imagePath := filepath.Join(imgDir, name+".jpg")
	if err := os.WriteFile(imagePath, []byte("fake jpg"), 0o644); err != nil {
		t.Fatal(err)
	}
	if labelBody != "" {
		if err := os.WriteFile(filepath.Join(lblDir, name+".txt"), []byte(labelBody), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return imagePath
}

func TestRoiLabelPathForImage(t *testing.T) {
	imagePath := filepath.Join("dataset", "images", "train", "clip_000001.jpg")
	got, err := roiLabelPathForImage(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("dataset", "labels", "train", "clip_000001.txt")
	if got != want {
		t.Fatalf("erwartete %q, bekam %q", want, got)
	}
}

func TestRoiLabelPathForImageAbsoluteKeepsRoot(t *testing.T) {
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "images", "train", "Zelda__000.jpg")
	got, err := roiLabelPathForImage(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join(dir, "labels", "train", "Zelda__000.txt")
	if got != want {
		t.Fatalf("absolute label path broke root: want %q got %q", want, got)
	}
	if !filepath.IsAbs(got) {
		t.Fatalf("label path must stay absolute, got %q", got)
	}
}

func TestRoiLabelPathForImageDriveLetterStyle(t *testing.T) {
	// Slash form used after filepath.ToSlash on Windows absolute paths.
	got, err := roiLabelPathForImage("C:/work/dataset/images/val/Zelda__001.jpg")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.FromSlash("C:/work/dataset/labels/val/Zelda__001.txt")
	if got != want {
		t.Fatalf("drive-letter path: want %q got %q", want, got)
	}
}

func TestRoiLabelPathForImageWithoutImagesDirIsAnError(t *testing.T) {
	if _, err := roiLabelPathForImage(filepath.Join("dataset", "clip.jpg")); err == nil {
		t.Fatal("erwartete einen Fehler ohne 'images'-Ordner im Pfad")
	}
}

func TestListRoiTrainingSamplesLoadsBoxesFromAbsoluteDataset(t *testing.T) {
	dir := t.TempDir()
	writeRoiSample(t, dir, "train", "Zelda__000", "0 0.42 0.55 0.18 0.22\n")
	if err := os.WriteFile(filepath.Join(dir, "classes.json"), []byte(`{"glans":0}`), 0o644); err != nil {
		t.Fatal(err)
	}

	a := NewApp()
	samples, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 {
		t.Fatalf("expected 1 sample, got %+v", samples)
	}
	if len(samples[0].Boxes) != 1 {
		t.Fatalf("expected boxes on absolute dataset path, got %+v", samples[0])
	}
	b := samples[0].Boxes[0]
	if b.XC != 0.42 || b.YC != 0.55 || b.W != 0.18 || b.H != 0.22 {
		t.Fatalf("box coords: %+v", b)
	}
	if b.ClassName != "glans" {
		t.Fatalf("className: %+v", b)
	}
}

func TestReadRoiLabelFileRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "sample.txt")
	names := map[int]string{0: "eichel", 1: "brustwarze"}

	boxes := []RoiTrainingBox{
		{ClassID: 0, ClassName: "eichel", XC: 0.5, YC: 0.5, W: 0.2, H: 0.2},
		{ClassID: 1, ClassName: "brustwarze", XC: 0.3, YC: 0.4, W: 0.1, H: 0.1},
	}
	if err := writeRoiLabelFile(path, boxes); err != nil {
		t.Fatal(err)
	}
	got, err := readRoiLabelFile(path, names)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 {
		t.Fatalf("erwartete 2 Boxen, bekam %d: %+v", len(got), got)
	}
	if got[0].ClassName != "eichel" || got[1].ClassName != "brustwarze" {
		t.Fatalf("Klassennamen wurden nicht richtig aus classes.json aufgelöst: %+v", got)
	}
	if got[0].XC != 0.5 || got[0].YC != 0.5 {
		t.Fatalf("Koordinaten stimmen nach dem Rundlauf nicht: %+v", got[0])
	}
}

func TestReadRoiLabelFileMissingFileReturnsEmpty(t *testing.T) {
	dir := t.TempDir()
	got, err := readRoiLabelFile(filepath.Join(dir, "fehlt.txt"), nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 0 {
		t.Fatalf("erwartete leere Liste ohne Labeldatei, bekam %+v", got)
	}
}

func TestReadRoiLabelFileSkipsMalformedLines(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "sample.txt")
	if err := os.WriteFile(path, []byte("0 0.5 0.5 0.2 0.2\nkaputte zeile\n1 0.1 0.1 0.05 0.05\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	got, err := readRoiLabelFile(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 {
		t.Fatalf("kaputte Zeile hätte übersprungen werden müssen, bekam %d Boxen: %+v", len(got), got)
	}
}

func TestListRoiTrainingSamplesFiltersByPrefix(t *testing.T) {
	dir := t.TempDir()
	writeRoiSample(t, dir, "train", "videoA_000000", "0 0.5 0.5 0.2 0.2\n")
	writeRoiSample(t, dir, "train", "videoA_000001", "0 0.4 0.4 0.2 0.2\n")
	writeRoiSample(t, dir, "val", "videoB_000000", "1 0.6 0.6 0.1 0.1\n")

	a := NewApp()

	all, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(all) != 3 {
		t.Fatalf("ohne Präfix erwartete 3 Beispiele (train+val), bekam %d: %+v", len(all), all)
	}

	filtered, err := a.ListRoiTrainingSamples(dir, "videoA")
	if err != nil {
		t.Fatal(err)
	}
	if len(filtered) != 2 {
		t.Fatalf("mit Präfix 'videoA' erwartete 2 Beispiele, bekam %d: %+v", len(filtered), filtered)
	}
	for _, s := range filtered {
		if s.Split != "train" {
			t.Fatalf("videoA-Beispiele sollten aus 'train' kommen, bekam %+v", s)
		}
	}
}

func TestUpdateAndDiscardRoiTrainingSample(t *testing.T) {
	dir := t.TempDir()
	writeRoiSample(t, dir, "train", "clip_000000", "0 0.5 0.5 0.2 0.2\n")

	a := NewApp()

	newBoxes := []RoiTrainingBox{{ClassID: 0, XC: 0.6, YC: 0.6, W: 0.15, H: 0.15}}
	if err := a.UpdateRoiTrainingSample(dir, "train", "clip_000000.jpg", newBoxes); err != nil {
		t.Fatal(err)
	}
	samples, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 || len(samples[0].Boxes) != 1 || samples[0].Boxes[0].XC != 0.6 {
		t.Fatalf("Korrektur wurde nicht übernommen: %+v", samples)
	}

	if err := a.DiscardRoiTrainingSample(dir, "train", "clip_000000.jpg"); err != nil {
		t.Fatal(err)
	}
	afterDiscard, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(afterDiscard) != 0 {
		t.Fatalf("erwartete leere Liste nach dem Verwerfen, bekam %+v", afterDiscard)
	}
	if _, err := os.Stat(filepath.Join(dir, "images", "train", "clip_000000.jpg")); !os.IsNotExist(err) {
		t.Fatal("discard sollte auch die Bilddatei entfernen")
	}
}

func TestUpdateRoiTrainingSampleRegistersAnyTaxonomyLabel(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "classes.json"), []byte(`{"face":0}`), 0o644); err != nil {
		t.Fatal(err)
	}
	writeRoiSample(t, dir, "train", "clip_000000", "0 0.5 0.5 0.2 0.2\n")

	a := NewApp()
	// Relabel Face window → Glans (+ add Mouth) so review can set all taxonomy labels.
	boxes := []RoiTrainingBox{
		{ClassID: 0, ClassName: "glans", XC: 0.4, YC: 0.4, W: 0.1, H: 0.1},
		{ClassID: 0, ClassName: "mouth", XC: 0.6, YC: 0.6, W: 0.12, H: 0.12},
	}
	if err := a.UpdateRoiTrainingSample(dir, "train", "clip_000000.jpg", boxes); err != nil {
		t.Fatal(err)
	}
	samples, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 || len(samples[0].Boxes) != 2 {
		t.Fatalf("expected 2 boxes, got %+v", samples)
	}
	byName := map[string]RoiTrainingBox{}
	for _, b := range samples[0].Boxes {
		byName[b.ClassName] = b
	}
	if _, ok := byName["glans"]; !ok {
		t.Fatalf("glans not registered: %+v", samples[0].Boxes)
	}
	if _, ok := byName["mouth"]; !ok {
		t.Fatalf("mouth not registered: %+v", samples[0].Boxes)
	}
	if byName["glans"].ClassID == byName["mouth"].ClassID {
		t.Fatalf("expected distinct class ids: %+v", byName)
	}
	raw, err := os.ReadFile(filepath.Join(dir, "classes.json"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(raw), `"glans"`) || !strings.Contains(string(raw), `"mouth"`) {
		t.Fatalf("classes.json missing new labels: %s", raw)
	}
}

// Free tags / AI Korrektur: same class ID on two boxes (both nipples) must persist.
func TestUpdateRoiTrainingSampleAllowsDuplicateNipples(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "classes.json"), []byte(`{"nipples":0}`), 0o644); err != nil {
		t.Fatal(err)
	}
	writeRoiSample(t, dir, "train", "clip_000000", "0 0.3 0.4 0.08 0.08\n")

	a := NewApp()
	boxes := []RoiTrainingBox{
		{ClassID: 0, ClassName: "nipples", XC: 0.35, YC: 0.42, W: 0.08, H: 0.08},
		{ClassID: 0, ClassName: "nipples", XC: 0.55, YC: 0.42, W: 0.08, H: 0.08},
	}
	if err := a.UpdateRoiTrainingSample(dir, "train", "clip_000000.jpg", boxes); err != nil {
		t.Fatal(err)
	}
	samples, err := a.ListRoiTrainingSamples(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 || len(samples[0].Boxes) != 2 {
		t.Fatalf("expected 2 nipples boxes, got %+v", samples)
	}
	for i, b := range samples[0].Boxes {
		if b.ClassName != "nipples" {
			t.Fatalf("box %d class=%q want nipples", i, b.ClassName)
		}
	}
	if samples[0].Boxes[0].XC == samples[0].Boxes[1].XC {
		t.Fatalf("expected distinct geometry for L/R nipples: %+v", samples[0].Boxes)
	}
	labelPath := filepath.Join(dir, "labels", "train", "clip_000000.txt")
	raw, err := os.ReadFile(labelPath)
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(strings.TrimSpace(string(raw)), "\n")
	if len(lines) != 2 {
		t.Fatalf("label file want 2 lines, got %q", raw)
	}
	// Same class_id twice is valid YOLO (free tags, not one-slot-per-class).
	for _, line := range lines {
		if !strings.HasPrefix(strings.TrimSpace(line), "0 ") {
			t.Fatalf("expected class_id 0 for both nipples lines, got %q", line)
		}
	}
}

func TestGetRoiDatasetSummaryCountsPerClassAndSplit(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "classes.json"), []byte(`{"eichel":0,"brustwarze":1}`), 0o644); err != nil {
		t.Fatal(err)
	}
	writeRoiSample(t, dir, "train", "a", "0 0.5 0.5 0.2 0.2\n1 0.3 0.3 0.1 0.1\n")
	writeRoiSample(t, dir, "train", "b", "0 0.5 0.5 0.2 0.2\n")
	writeRoiSample(t, dir, "val", "c", "1 0.3 0.3 0.1 0.1\n")

	a := NewApp()
	summary, err := a.GetRoiDatasetSummary(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(summary.Classes) != 2 {
		t.Fatalf("erwartete 2 Klassen, bekam %+v", summary.Classes)
	}
	byName := map[string]RoiClassCount{}
	for _, c := range summary.Classes {
		byName[c.ClassName] = c
	}
	if byName["eichel"].TrainCount != 2 || byName["eichel"].ValCount != 0 {
		t.Fatalf("falsche Zählung für 'eichel': %+v", byName["eichel"])
	}
	if byName["brustwarze"].TrainCount != 1 || byName["brustwarze"].ValCount != 1 {
		t.Fatalf("falsche Zählung für 'brustwarze': %+v", byName["brustwarze"])
	}
}

func TestGetRoiDatasetSummaryWithoutClassesJsonIsNotAnError(t *testing.T) {
	dir := t.TempDir()
	a := NewApp()
	summary, err := a.GetRoiDatasetSummary(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(summary.Classes) != 0 {
		t.Fatalf("erwartete leere Klassenliste ohne classes.json, bekam %+v", summary.Classes)
	}
}

func TestGetRoiTrainingSampleImageReturnsBase64(t *testing.T) {
	dir := t.TempDir()
	imagePath := writeRoiSample(t, dir, "train", "clip_000000", "")

	a := NewApp()
	b64, err := a.GetRoiTrainingSampleImage(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		t.Fatalf("Ergebnis ist kein gültiges Base64: %v", err)
	}
	if string(decoded) != "fake jpg" {
		t.Fatalf("Base64 dekodiert nicht auf den Dateiinhalt: %q", decoded)
	}
}

func TestGetRoiTrainingSampleImageMissingFileIsAnError(t *testing.T) {
	dir := t.TempDir()
	a := NewApp()
	if _, err := a.GetRoiTrainingSampleImage(filepath.Join(dir, "fehlt.jpg")); err == nil {
		t.Fatal("erwartete einen Fehler für eine fehlende Bilddatei")
	}
}

func TestRoiTrainingSamplePrefixIsFilenameSafeAndUnique(t *testing.T) {
	p1 := roiTrainingSamplePrefix(`C:\videos\Mein Clip! (2026).mp4`)
	p2 := roiTrainingSamplePrefix(`C:\videos\Mein Clip! (2026).mp4`)
	for _, r := range p1 {
		if !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '_') {
			t.Fatalf("Präfix enthält ein dateinamen-unsicheres Zeichen: %q in %q", r, p1)
		}
	}
	if p1 == p2 {
		t.Fatal("zwei Aufrufe für denselben Videopfad sollten unterschiedliche Präfixe liefern (Zeitkomponente)")
	}
}
