package main

import (
	"bufio"
	"context"
	"crypto/sha1"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Eigener Namensraum "Roi..." statt "Training...": "Training" ist im Rest
// der App schon für Geräte-Trainingssitzungen (Stop-Start/Plateau, siehe
// app_training.go) belegt - hier geht es um das Trainieren eines
// KI-Regionserkennungsmodells, etwas völlig anderes.

// roiTrainingRunning verhindert einen zweiten gleichzeitigen Bootstrap-
// oder Trainingslauf - beide sind für Sekunden bis (beim Training) Stunden
// blockierend und schreiben in denselben Datensatzordner.
// Geschützt durch roiTrainingMu: claim läuft im Wails-Aufruf, release in
// der Worker-Goroutine (vorher eine ungeschützte bool - Data Race).
var (
	roiTrainingMu      sync.Mutex
	roiTrainingRunning bool
	roiTrainingCancel  context.CancelFunc
)

// claimRoiTrainingRun reserviert den Lauf und liefert seinen Kontext -
// CancelRoiTraining (Knopf, App-Ende) bricht ihn ab.
func claimRoiTrainingRun() (context.Context, error) {
	roiTrainingMu.Lock()
	defer roiTrainingMu.Unlock()
	if roiTrainingRunning {
		return nil, fmt.Errorf("a bootstrap or training run is already in progress")
	}
	ctx, cancel := context.WithCancel(context.Background())
	roiTrainingRunning = true
	roiTrainingCancel = cancel
	return ctx, nil
}

func releaseRoiTrainingRun() {
	roiTrainingMu.Lock()
	defer roiTrainingMu.Unlock()
	if roiTrainingCancel != nil {
		roiTrainingCancel()
	}
	roiTrainingRunning = false
	roiTrainingCancel = nil
}

// CancelRoiTraining bricht einen laufenden Bootstrap- oder Trainingslauf ab
// (der Python-Prozess wird beendet; "done" meldet dann den Abbruch).
// Liefert false, wenn nichts lief. Auch beim Schließen der App aufgerufen -
// vorher lief ein stundenlanges Training danach unsichtbar weiter.
func (a *App) CancelRoiTraining() bool {
	if requireSuite() != nil {
		return false
	}
	roiTrainingMu.Lock()
	defer roiTrainingMu.Unlock()
	if !roiTrainingRunning || roiTrainingCancel == nil {
		return false
	}
	roiTrainingCancel()
	logging.Info("roitraining: run cancelled")
	return true
}

// roiTrainingSamplePrefix erzeugt einen stabilen, dateinamentauglichen
// Präfix je AUFRUF (nicht nur je Video) - anders als Pythons interner
// _video_stem() (rein vom Videopfad abgeleitet, kollidiert bei einem
// zweiten Bootstrap-Lauf auf demselben Video mit sample_idx wieder bei 0).
// Die Zeitkomponente macht jeden Lauf eindeutig UND lässt die
// Kontrollansicht (ListRoiTrainingSamples) exakt auf "gerade eben
// hinzugekommen" eingrenzen, nicht auf "irgendwann für dieses Video".
func roiTrainingSamplePrefix(videoPath string) string {
	base := strings.TrimSuffix(filepath.Base(videoPath), filepath.Ext(videoPath))
	var safe strings.Builder
	for _, r := range base {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') {
			safe.WriteRune(r)
		} else {
			safe.WriteRune('_')
		}
	}
	safeName := safe.String()
	if len(safeName) > 40 {
		safeName = safeName[:40]
	}
	h := sha1.Sum([]byte(videoPath + time.Now().Format(time.RFC3339Nano)))
	return fmt.Sprintf("%s_%x", safeName, h[:5])
}

// CheckRoiTrainingAvailable meldet, ob das eigentliche Trainieren (nicht das
// Sammeln von Trainingsdaten - das braucht nur die Basis-Installation)
// grundsätzlich nutzbar ist, also ob ultralytics installiert ist. Die GUI
// nutzt das, um den "Training starten"-Knopf zu aktivieren/auszublenden
// statt ihn anzubieten und dann mit einem rohen Python-Traceback scheitern
// zu lassen (siehe CheckAIRoiAvailable für dasselbe Muster).
func (a *App) CheckRoiTrainingAvailable() bool {
	if requireSuite() != nil {
		return false
	}
	return generator.RoiTrainingAvailable()
}

// CheckRoiTrainingStatus liefert getrennte Flags für Python/OpenCV/ultralytics.
func (a *App) CheckRoiTrainingStatus() generator.RoiTrainingStatus {
	if requireSuite() != nil {
		return generator.RoiTrainingStatus{}
	}
	return generator.GetRoiTrainingStatus()
}

// InstallRoiTrainingDeps installiert ultralytics+onnx per pip (asynchron,
// Events roitraining:deps:progress|done). Für Release-Builds ohne
// Quellbaum — requirements sind im Binary eingebettet.
func (a *App) InstallRoiTrainingDeps() error {
	if err := requireSuite(); err != nil {
		return err
	}
	if _, err := claimRoiTrainingRun(); err != nil {
		return err
	}
	go func() {
		defer releaseRoiTrainingRun()
		err := generator.InstallRoiTrainingDeps(func(line string) {
			runtime.EventsEmit(a.ctx, "roitraining:deps:progress", line)
		})
		if err != nil {
			runtime.EventsEmit(a.ctx, "roitraining:deps:done", map[string]any{"error": err.Error()})
			return
		}
		runtime.EventsEmit(a.ctx, "roitraining:deps:done", map[string]any{
			"ok":     true,
			"status": generator.GetRoiTrainingStatus(),
		})
	}()
	return nil
}

// WriteAIRequirementFiles schreibt die eingebetteten requirements-*.txt
// in einen Ordner (manuelles pip install).
func (a *App) WriteAIRequirementFiles(dir string) error {
	if err := requireSuite(); err != nil {
		return err
	}
	return generator.WriteAIRequirementFiles(dir)
}

// ListRoiTrainingDevices liefert die wählbaren Trainingsgeräte
// (auto/cuda/directml/mps/cpu) inkl. aktueller Verfügbarkeit - siehe
// generator.ListRoiTrainingDevices / train_yolo_model --list-devices.
func (a *App) ListRoiTrainingDevices() []generator.RoiTrainingDevice {
	if requireSuite() != nil {
		return nil
	}
	return generator.ListRoiTrainingDevices()
}

// BootstrapRoiTrainingSample trackt roi (und optional weitere Regionen) durchs
// Video und hängt die Ergebnisse an den Datensatz an. sampleEvery steuert die
// Abtastung (Standard 12); extractAudio speichert die Tonspur neben dem Datensatz.
func (a *App) BootstrapRoiTrainingSample(videoPath string, roi, roi2 *generator.ROI, className, className2 string) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	return a.BootstrapRoiTrainingSampleEx(videoPath, roi, roi2, nil, nil, className, className2, "", "", 12, true, 0, 1.0)
}

// BootstrapRoiTrainingSampleEx supports up to 4 marks (compat). Prefer
// BootstrapRoiTrainingRegions for the full body-parts taxonomy (9 marks).
func (a *App) BootstrapRoiTrainingSampleEx(
	videoPath string,
	roi, roi2, roi3, roi4 *generator.ROI,
	className, className2, className3, className4 string,
	sampleEvery int,
	extractAudio bool,
	startSeconds float64,
	boxScale float64,
) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	if roi == nil {
		return "", fmt.Errorf("at least one region (ROI1) is required")
	}
	regions := []generator.RoiTrainingRegion{{ROI: *roi, ClassName: className}}
	if roi2 != nil {
		regions = append(regions, generator.RoiTrainingRegion{ROI: *roi2, ClassName: className2})
	}
	if roi3 != nil {
		regions = append(regions, generator.RoiTrainingRegion{ROI: *roi3, ClassName: className3})
	}
	if roi4 != nil {
		regions = append(regions, generator.RoiTrainingRegion{ROI: *roi4, ClassName: className4})
	}
	return a.BootstrapRoiTrainingRegions(videoPath, regions, sampleEvery, extractAudio, startSeconds, boxScale)
}

// BootstrapRoiTrainingRegions tracks 1–MaxRegionsPerImage free tag boxes
// through the video into the YOLO dataset (docs/BODY_REGIONS.md). Every
// labeled box is tracked (nipples/breasts/…), not tip/glans alone.
func (a *App) BootstrapRoiTrainingRegions(
	videoPath string,
	regions []generator.RoiTrainingRegion,
	sampleEvery int,
	extractAudio bool,
	startSeconds float64,
	boxScale float64,
) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	runCtx, err := claimRoiTrainingRun()
	if err != nil {
		return "", err
	}
	if len(regions) == 0 {
		releaseRoiTrainingRun()
		return "", fmt.Errorf("at least one region is required")
	}
	for i, r := range regions {
		if strings.TrimSpace(r.ClassName) == "" {
			releaseRoiTrainingRun()
			return "", fmt.Errorf("region %d needs a class name", i+1)
		}
	}
	if sampleEvery <= 0 {
		sampleEvery = 12
	}
	if boxScale <= 0 {
		boxScale = 1.0
	}

	datasetDir := a.settings.GetString(prefRoiDatasetDir, generator.DefaultRoiDatasetDir())
	prefix := roiTrainingSamplePrefix(videoPath)

	go func() {
		defer releaseRoiTrainingRun()
		err := generator.BootstrapRoiTrainingSampleOptsCtx(runCtx, videoPath, regions, datasetDir, prefix, sampleEvery, extractAudio, startSeconds, boxScale,
			func(line string) { runtime.EventsEmit(a.ctx, "roitraining:bootstrap:progress", line) },
			func(pct int) { runtime.EventsEmit(a.ctx, "roitraining:bootstrap:percent", pct) })
		if err != nil {
			logging.Error("roitraining: bootstrap failed", "video", videoPath, "error", err)
			runtime.EventsEmit(a.ctx, "roitraining:bootstrap:done", map[string]any{"error": err.Error()})
			return
		}
		logging.Info("roitraining: bootstrap finished", "video", videoPath, "prefix", prefix)
		runtime.EventsEmit(a.ctx, "roitraining:bootstrap:done", map[string]any{"prefix": prefix})
	}()

	return prefix, nil
}

// AddRoiStillTrainingSample adds one labeled still image (photo) to the dataset.
func (a *App) AddRoiStillTrainingSample(imagePath string, regions []generator.RoiTrainingRegion) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	if len(regions) == 0 {
		return "", fmt.Errorf("at least one region required")
	}
	datasetDir := a.settings.GetString(prefRoiDatasetDir, generator.DefaultRoiDatasetDir())
	prefix := roiTrainingSamplePrefix(imagePath)
	return generator.AddStillTrainingSample(imagePath, regions, datasetDir, prefix)
}

// PickImageFile opens an image picker for still-image training samples.
func (a *App) PickImageFile() (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose image for AI training",
		Filters: []runtime.FileFilter{
			{DisplayName: "Images", Pattern: "*.jpg;*.jpeg;*.png;*.webp"},
		},
	})
}

// RunRoiModelTraining trainiert ein Modell auf dem gesammelten Datensatz und
// legt es unter dem KI-Modellpfad der Einstellungen ab (derselbe Pfad, den
// ai_roi.default_model_path()/CheckAIRoiAvailable() prüfen) - nach Erfolg
// ist die KI-Erkennung im Generator-Tab also ohne weiteren Schritt nutzbar.
// Läuft asynchron, kann bei einem echten Datensatz lange dauern.
func (a *App) RunRoiModelTraining(epochs int, device string) error {
	if err := requireSuite(); err != nil {
		return err
	}
	runCtx, err := claimRoiTrainingRun()
	if err != nil {
		return err
	}

	datasetDir := a.settings.GetString(prefRoiDatasetDir, generator.DefaultRoiDatasetDir())
	modelPath := a.settings.GetString(prefAIRoiModelPath, "")
	if modelPath == "" {
		modelPath = defaultAIRoiModelPath()
	}

	go func() {
		defer releaseRoiTrainingRun()
		if err := os.MkdirAll(filepath.Dir(modelPath), 0o755); err != nil {
			runtime.EventsEmit(a.ctx, "roitraining:train:done", map[string]any{"error": err.Error()})
			return
		}
		err := generator.RunRoiModelTrainingCtx(runCtx, datasetDir, modelPath, epochs, device,
			func(line string) { runtime.EventsEmit(a.ctx, "roitraining:train:progress", line) },
			func(pct int) { runtime.EventsEmit(a.ctx, "roitraining:train:percent", pct) })
		if err != nil {
			logging.Error("roitraining: training failed", "error", err)
			runtime.EventsEmit(a.ctx, "roitraining:train:done", map[string]any{"error": err.Error()})
			return
		}
		logging.Info("roitraining: training finished", "model", modelPath)
		runtime.EventsEmit(a.ctx, "roitraining:train:done", map[string]any{"modelPath": modelPath})
	}()

	return nil
}

// RoiTrainingBox ist eine einzelne markierte Region in einem Trainingsbild -
// normalisierte Koordinaten (0..1), genau das YOLO-Labelformat.
type RoiTrainingBox struct {
	ClassID   int     `json:"classId"`
	ClassName string  `json:"className"`
	XC        float64 `json:"xc"`
	YC        float64 `json:"yc"`
	W         float64 `json:"w"`
	H         float64 `json:"h"`
}

// RoiTrainingSample ist ein Bootstrap-Beispiel für die Kontrollansicht.
type RoiTrainingSample struct {
	Split     string           `json:"split"`
	Name      string           `json:"name"`
	ImagePath string           `json:"imagePath"`
	Boxes     []RoiTrainingBox `json:"boxes"`
}

// roiTrainingClassNames liest classes.json (Name -> ID, siehe
// bootstrap_yolo_dataset.py register_class) und dreht die Zuordnung um, für
// die Anzeige in der Kontrollansicht. Eine fehlende Datei ist kein Fehler -
// vor dem ersten Bootstrap-Lauf existiert sie schlicht noch nicht.
func roiTrainingClassNames(datasetDir string) (map[int]string, error) {
	path := filepath.Join(datasetDir, "classes.json")
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return map[int]string{}, nil
		}
		return nil, err
	}
	var registry map[string]int
	if err := json.Unmarshal(data, &registry); err != nil {
		return nil, fmt.Errorf("classes.json invalid: %w", err)
	}
	names := make(map[int]string, len(registry))
	for name, id := range registry {
		names[id] = name
	}
	return names, nil
}

// readRoiLabelFile parst eine YOLO-Labeldatei ("class xc yc w h" je Zeile) -
// genau dasselbe simple Format wie bootstrap_yolo_dataset.py's
// read_label_file/write_label_file, hier direkt in Go statt über einen
// Python-Unterprozess (reine Textdatei, kein cv2/numpy nötig - analog zu
// GetBenchmarkHistory's JSONL-Lesen ohne Python-Umweg).
func readRoiLabelFile(path string, classNames map[int]string) ([]RoiTrainingBox, error) {
	f, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	defer f.Close()

	var boxes []RoiTrainingBox
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) != 5 {
			continue
		}
		classID, err := strconv.Atoi(fields[0])
		if err != nil {
			continue
		}
		vals := make([]float64, 4)
		ok := true
		for i, s := range fields[1:] {
			v, err := strconv.ParseFloat(s, 64)
			if err != nil {
				ok = false
				break
			}
			vals[i] = v
		}
		if !ok {
			continue
		}
		boxes = append(boxes, RoiTrainingBox{
			ClassID: classID, ClassName: classNames[classID],
			XC: vals[0], YC: vals[1], W: vals[2], H: vals[3],
		})
	}
	return boxes, scanner.Err()
}

func writeRoiLabelFile(path string, boxes []RoiTrainingBox) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	var sb strings.Builder
	for _, b := range boxes {
		fmt.Fprintf(&sb, "%d %.6f %.6f %.6f %.6f\n", b.ClassID, b.XC, b.YC, b.W, b.H)
	}
	return os.WriteFile(path, []byte(sb.String()), 0o644)
}

// roiLabelPathForImage maps …/images/<split>/<name>.jpg → …/labels/<split>/<name>.txt.
// Uses slash-string replace (not filepath.Join on split segments) so absolute
// paths stay absolute: Join("","tmp",…) drops the root, and on Windows
// Join("C:","work",…) yields the relative "C:work\…" form — both made the
// review grid load images with empty Boxes (no overlays) while Discard still
// removed the image via the correctly-joined imagePath.
func roiLabelPathForImage(imagePath string) (string, error) {
	slash := filepath.ToSlash(imagePath)
	const marker = "/images/"
	idx := strings.LastIndex(slash, marker)
	var replaced string
	switch {
	case idx >= 0:
		replaced = slash[:idx] + "/labels/" + slash[idx+len(marker):]
	case strings.HasPrefix(slash, "images/"):
		replaced = "labels/" + strings.TrimPrefix(slash, "images/")
	default:
		return "", fmt.Errorf("no 'images' folder in path: %s", imagePath)
	}
	ext := filepath.Ext(replaced)
	return filepath.FromSlash(strings.TrimSuffix(replaced, ext) + ".txt"), nil
}

// ListRoiTrainingSamples listet Trainingsbeispiele aus datasetDir/images/*
// mit prefix im Dateinamen (siehe roiTrainingSamplePrefix) - die
// Kontrollansicht nach einem Bootstrap-Lauf. Läuft synchron: reines
// Verzeichnislisten + Textdateien lesen, kein Unterprozess nötig.
func (a *App) ListRoiTrainingSamples(datasetDir, prefix string) ([]RoiTrainingSample, error) {
	if err := requireSuite(); err != nil {
		return nil, err
	}
	classNames, err := roiTrainingClassNames(datasetDir)
	if err != nil {
		return nil, err
	}
	var samples []RoiTrainingSample
	for _, split := range []string{"train", "val"} {
		imgDir := filepath.Join(datasetDir, "images", split)
		entries, err := os.ReadDir(imgDir)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return nil, err
		}
		for _, e := range entries {
			if e.IsDir() {
				continue
			}
			name := e.Name()
			lower := strings.ToLower(name)
			if !strings.HasSuffix(lower, ".jpg") && !strings.HasSuffix(lower, ".jpeg") && !strings.HasSuffix(lower, ".png") {
				continue
			}
			if prefix != "" && !strings.HasPrefix(name, prefix) {
				continue
			}
			imagePath := filepath.Join(imgDir, name)
			labelPath, err := roiLabelPathForImage(imagePath)
			if err != nil {
				continue
			}
			boxes, err := readRoiLabelFile(labelPath, classNames)
			if err != nil {
				return nil, err
			}
			samples = append(samples, RoiTrainingSample{
				Split: split, Name: name, ImagePath: imagePath, Boxes: boxes,
			})
		}
	}
	sort.Slice(samples, func(i, j int) bool { return samples[i].Name < samples[j].Name })
	return samples, nil
}

// GetRoiTrainingSampleImage liefert ein Trainingsbild base64-kodiert - roh
// als file:// eingebunden lehnt die Wails-Webview <img src> ab (siehe
// LoadFirstFrame in app_generator.go, dieselbe Einschränkung), deshalb geht
// jedes Bild für die Kontrollansicht denselben Weg über die RPC-Brücke.
func (a *App) GetRoiTrainingSampleImage(imagePath string) (string, error) {
	if err := requireSuite(); err != nil {
		return "", err
	}
	return fileToBase64(imagePath)
}

// UpdateRoiTrainingSample überschreibt die Boxen eines Beispiels - für
// Korrekturen aus der Kontrollansicht (falsch/ungenau getrackte Box von
// Hand richten, statt sie unkontrolliert ins Training gehen zu lassen).
// ClassName is resolved through the product taxonomy and registered in
// classes.json so review can set any detection label (not only Face / id 0).
func (a *App) UpdateRoiTrainingSample(datasetDir, split, name string, boxes []RoiTrainingBox) error {
	if err := requireSuite(); err != nil {
		return err
	}
	imagePath := filepath.Join(datasetDir, "images", split, name)
	labelPath, err := roiLabelPathForImage(imagePath)
	if err != nil {
		return err
	}
	resolved, err := resolveRoiTrainingBoxes(datasetDir, boxes)
	if err != nil {
		return err
	}
	return writeRoiLabelFile(labelPath, resolved)
}

// resolveRoiTrainingBoxes maps ClassName → ClassID via classes.json,
// registering new canonical names when the review editor assigns them.
// Empty ClassName keeps the existing ClassID (geometry-only adjust).
func resolveRoiTrainingBoxes(datasetDir string, boxes []RoiTrainingBox) ([]RoiTrainingBox, error) {
	names := make([]string, 0, len(boxes))
	for _, b := range boxes {
		if n := bodyparts.Normalize(b.ClassName); n != "" {
			names = append(names, n)
		}
	}
	reg, err := generator.EnsureClassIDs(datasetDir, names)
	if err != nil {
		return nil, err
	}
	out := make([]RoiTrainingBox, len(boxes))
	for i, b := range boxes {
		out[i] = b
		n := bodyparts.Normalize(b.ClassName)
		if n == "" {
			continue
		}
		if id, ok := reg[n]; ok {
			out[i].ClassName = n
			out[i].ClassID = id
			continue
		}
		// Legacy German registry key that Normalize maps to n.
		for k, id := range reg {
			if bodyparts.Normalize(k) == n {
				out[i].ClassName = k
				out[i].ClassID = id
				break
			}
		}
	}
	return out, nil
}

// DiscardRoiTrainingSample entfernt ein Beispiel vollständig (Bild +
// Labeldatei) - für Frames, die die Kontrollansicht als falsch verwirft.
func (a *App) DiscardRoiTrainingSample(datasetDir, split, name string) error {
	if err := requireSuite(); err != nil {
		return err
	}
	imagePath := filepath.Join(datasetDir, "images", split, name)
	labelPath, err := roiLabelPathForImage(imagePath)
	if err != nil {
		return err
	}
	for _, p := range []string{imagePath, labelPath} {
		if err := os.Remove(p); err != nil && !os.IsNotExist(err) {
			return err
		}
	}
	return nil
}

// RoiClassCount ist die Anzahl Beispiele je Klasse und Split - für die
// Datensatz-Übersicht.
type RoiClassCount struct {
	ClassName  string `json:"className"`
	ClassID    int    `json:"classId"`
	TrainCount int    `json:"trainCount"`
	ValCount   int    `json:"valCount"`
}

// RoiDatasetSummary fasst den aktuellen Trainingsdatensatz zusammen.
type RoiDatasetSummary struct {
	DatasetDir   string          `json:"datasetDir"`
	HasDataYAML  bool            `json:"hasDataYaml"`
	TrainImages  int             `json:"trainImages"`
	ReadyToTrain bool            `json:"readyToTrain"`
	Classes      []RoiClassCount `json:"classes"`
}

// GetRoiDatasetSummary liest classes.json und zählt, wie oft jede Klasse in
// den Labeldateien je Split vorkommt - Grundlage der "Datensatz-Übersicht"
// in der Trainings-Oberfläche (siehe docs für den vollen Ablauf).
func (a *App) GetRoiDatasetSummary(datasetDir string) (RoiDatasetSummary, error) {
	if err := requireSuite(); err != nil {
		return RoiDatasetSummary{}, err
	}
	summary := RoiDatasetSummary{DatasetDir: datasetDir}
	if _, err := os.Stat(filepath.Join(datasetDir, "data.yaml")); err == nil {
		summary.HasDataYAML = true
	}
	if entries, err := os.ReadDir(filepath.Join(datasetDir, "images", "train")); err == nil {
		for _, e := range entries {
			if e.IsDir() {
				continue
			}
			n := strings.ToLower(e.Name())
			if strings.HasSuffix(n, ".jpg") || strings.HasSuffix(n, ".jpeg") ||
				strings.HasSuffix(n, ".png") || strings.HasSuffix(n, ".webp") {
				summary.TrainImages++
			}
		}
	}
	summary.ReadyToTrain = summary.HasDataYAML && summary.TrainImages > 0
	classNames, err := roiTrainingClassNames(datasetDir)
	if err != nil {
		// Empty dataset (no classes.json yet) is normal before first sample.
		if os.IsNotExist(err) {
			return summary, nil
		}
		return summary, err
	}
	counts := make(map[int]*RoiClassCount, len(classNames))
	for id, name := range classNames {
		counts[id] = &RoiClassCount{ClassName: name, ClassID: id}
	}
	for _, split := range []string{"train", "val"} {
		lblDir := filepath.Join(datasetDir, "labels", split)
		entries, err := os.ReadDir(lblDir)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return summary, err
		}
		for _, e := range entries {
			if e.IsDir() || !strings.HasSuffix(e.Name(), ".txt") {
				continue
			}
			boxes, err := readRoiLabelFile(filepath.Join(lblDir, e.Name()), classNames)
			if err != nil {
				return summary, err
			}
			for _, b := range boxes {
				c, ok := counts[b.ClassID]
				if !ok {
					c = &RoiClassCount{ClassName: b.ClassName, ClassID: b.ClassID}
					counts[b.ClassID] = c
				}
				if split == "train" {
					c.TrainCount++
				} else {
					c.ValCount++
				}
			}
		}
	}
	for _, c := range counts {
		summary.Classes = append(summary.Classes, *c)
	}
	sort.Slice(summary.Classes, func(i, j int) bool { return summary.Classes[i].ClassID < summary.Classes[j].ClassID })
	return summary, nil
}
