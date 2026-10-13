package generator

import (
	"context"
	"encoding/json"
	"fmt"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// AddStillTrainingSample writes one labeled still image into the YOLO
// dataset (train split). Used for learning from photos without video
// tracking — marks transfer later via clip bootstrap.
func AddStillTrainingSample(imagePath string, regions []RoiTrainingRegion, outputDir, samplePrefix string) (string, error) {
	if len(regions) == 0 {
		return "", fmt.Errorf("generator: at least one region required")
	}
	if strings.TrimSpace(outputDir) == "" {
		return "", fmt.Errorf("generator: outputDir leer")
	}
	w, h, err := stillImageSize(imagePath)
	if err != nil {
		return "", fmt.Errorf("generator: Bild lesen: %w", err)
	}
	if w < 8 || h < 8 {
		return "", fmt.Errorf("generator: Bild zu klein (%dx%d)", w, h)
	}

	registry, err := loadClassRegistry(outputDir)
	if err != nil {
		return "", err
	}
	var lines []string
	for _, r := range regions {
		name := strings.TrimSpace(r.ClassName)
		if name == "" {
			return "", fmt.Errorf("generator: Klassenname fehlt")
		}
		id := assignClassID(registry, name)
		xc := (float64(r.ROI.X) + float64(r.ROI.W)/2) / float64(w)
		yc := (float64(r.ROI.Y) + float64(r.ROI.H)/2) / float64(h)
		nw := float64(r.ROI.W) / float64(w)
		nh := float64(r.ROI.H) / float64(h)
		if nw <= 0 || nh <= 0 {
			continue
		}
		lines = append(lines, fmt.Sprintf("%d %.6f %.6f %.6f %.6f", id, xc, yc, nw, nh))
	}
	if len(lines) == 0 {
		return "", fmt.Errorf("generator: no valid boxes")
	}
	if err := saveClassRegistry(outputDir, registry); err != nil {
		return "", err
	}

	prefix := samplePrefix
	if prefix == "" {
		base := strings.TrimSuffix(filepath.Base(imagePath), filepath.Ext(imagePath))
		prefix = "still_" + sanitizeStem(base)
	}

	// Alternate into val when train already has samples — empty val breaks
	// ultralytics (data.yaml points at images/val).
	split := "train"
	if shouldUseValSplit(outputDir) {
		split = "val"
	}
	if err := os.MkdirAll(filepath.Join(outputDir, "images", split), 0o755); err != nil {
		return "", err
	}
	if err := os.MkdirAll(filepath.Join(outputDir, "labels", split), 0o755); err != nil {
		return "", err
	}

	stem := fmt.Sprintf("%s_000000", prefix)
	ext := strings.ToLower(filepath.Ext(imagePath))
	dstImg := filepath.Join(outputDir, "images", split, stem+ext)
	needConvert := ext != ".jpg" && ext != ".jpeg" && ext != ".png"
	if needConvert {
		dstImg = filepath.Join(outputDir, "images", split, stem+".jpg")
		if err := convertStillToJPEG(imagePath, dstImg); err != nil {
			return "", err
		}
	} else {
		if err := copyFile(imagePath, dstImg); err != nil {
			return "", err
		}
	}
	lbl := filepath.Join(outputDir, "labels", split, stem+".txt")
	if err := os.WriteFile(lbl, []byte(strings.Join(lines, "\n")+"\n"), 0o644); err != nil {
		return "", err
	}
	_ = writeDataYAMLFromRegistry(outputDir, registry)
	return prefix, nil
}

// shouldUseValSplit puts roughly every 6th still into val when train already
// has material, so still-only datasets are not val-empty.
func shouldUseValSplit(outputDir string) bool {
	trainN := countImages(filepath.Join(outputDir, "images", "train"))
	valN := countImages(filepath.Join(outputDir, "images", "val"))
	if trainN == 0 {
		return false // first sample always train
	}
	if valN == 0 && trainN >= 1 {
		return true // guarantee at least one val after the first train
	}
	return (trainN+valN)%6 == 5
}

func countImages(dir string) int {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return 0
	}
	n := 0
	for _, e := range ents {
		if e.IsDir() {
			continue
		}
		ext := strings.ToLower(filepath.Ext(e.Name()))
		if ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" {
			n++
		}
	}
	return n
}

// listImageNames returns sorted image basenames in dir (same extensions as countImages).
func listImageNames(dir string) []string {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var names []string
	for _, e := range ents {
		if e.IsDir() {
			continue
		}
		ext := strings.ToLower(filepath.Ext(e.Name()))
		if ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" {
			names = append(names, e.Name())
		}
	}
	return names
}

// EnsureRoiDatasetSplits copies one image+label into an empty train/val split
// so Ultralytics never sees empty images/val (AssertionError / FileNotFoundError).
// Returns train and val image counts after healing.
func EnsureRoiDatasetSplits(datasetDir string) (trainN, valN int, err error) {
	imgTrain := filepath.Join(datasetDir, "images", "train")
	imgVal := filepath.Join(datasetDir, "images", "val")
	lblTrain := filepath.Join(datasetDir, "labels", "train")
	lblVal := filepath.Join(datasetDir, "labels", "val")
	for _, d := range []string{imgTrain, imgVal, lblTrain, lblVal} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			return 0, 0, err
		}
	}
	trainImgs := listImageNames(imgTrain)
	valImgs := listImageNames(imgVal)

	copyOne := func(srcImgDir, srcLblDir, dstImgDir, dstLblDir, name string) error {
		srcImg := filepath.Join(srcImgDir, name)
		dstImg := filepath.Join(dstImgDir, name)
		if _, err := os.Stat(dstImg); os.IsNotExist(err) {
			if err := copyFile(srcImg, dstImg); err != nil {
				return err
			}
		}
		stem := strings.TrimSuffix(name, filepath.Ext(name))
		srcLbl := filepath.Join(srcLblDir, stem+".txt")
		dstLbl := filepath.Join(dstLblDir, stem+".txt")
		if _, err := os.Stat(srcLbl); err == nil {
			if _, err := os.Stat(dstLbl); os.IsNotExist(err) {
				if err := copyFile(srcLbl, dstLbl); err != nil {
					return err
				}
			}
		}
		return nil
	}

	switch {
	case len(valImgs) == 0 && len(trainImgs) > 0:
		if err := copyOne(imgTrain, lblTrain, imgVal, lblVal, trainImgs[0]); err != nil {
			return 0, 0, fmt.Errorf("generator: heal empty val split: %w", err)
		}
	case len(trainImgs) == 0 && len(valImgs) > 0:
		if err := copyOne(imgVal, lblVal, imgTrain, lblTrain, valImgs[0]); err != nil {
			return 0, 0, fmt.Errorf("generator: heal empty train split: %w", err)
		}
	}
	return countImages(imgTrain), countImages(imgVal), nil
}

// PrepareRoiDatasetForTraining heals empty splits and returns a clear error when
// there are no labeled images left to train on.
func PrepareRoiDatasetForTraining(datasetDir string) error {
	trainN, valN, err := EnsureRoiDatasetSplits(datasetDir)
	if err != nil {
		return err
	}
	if trainN < 1 || valN < 1 {
		return fmt.Errorf(
			"training dataset is empty under %s (train=%d, val=%d). Need at least 1 labeled image. In AI Train: mark region(s), click “Use for training”, keep at least one sample, then start training again",
			datasetDir, trainN, valN)
	}
	return nil
}

// stillImageSize returns pixel size for JPEG/PNG via stdlib, or via ffmpeg
// for formats Go does not decode (WebP, HEIC, …).
func stillImageSize(path string) (int, int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, 0, err
	}
	cfg, _, err := image.DecodeConfig(f)
	f.Close()
	if err == nil {
		return cfg.Width, cfg.Height, nil
	}
	w, h, ffErr := imageSizeViaFFmpeg(path)
	if ffErr != nil {
		return 0, 0, err
	}
	return w, h, nil
}

// Bounded, per-call contexts (rather than nil/Background) so a hung or
// oversized still/clip can't leave an unkillable ffmpeg/ffprobe process
// behind — MT-Infra ctx-kill hygiene. This tooling has no natural caller
// context to thread through (dataset bootstrap, not a live request), so
// each function derives its own budget instead of accepting a ctx param.

func imageSizeViaFFmpeg(path string) (int, int, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	cmd, err := videox.ProbeCommandContext(ctx, "-v", "error", "-select_streams", "v:0",
		"-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", path)
	if err != nil {
		return 0, 0, fmt.Errorf("ffprobe nicht gefunden")
	}
	out, err := cmd.CombinedOutput()
	if err != nil {
		return 0, 0, fmt.Errorf("ffprobe: %w (%s)", err, strings.TrimSpace(string(out)))
	}
	parts := strings.Split(strings.TrimSpace(string(out)), "x")
	if len(parts) != 2 {
		return 0, 0, fmt.Errorf("ffprobe: unerwartete Ausgabe %q", string(out))
	}
	w, err1 := strconv.Atoi(parts[0])
	h, err2 := strconv.Atoi(parts[1])
	if err1 != nil || err2 != nil || w < 1 || h < 1 {
		return 0, 0, fmt.Errorf("ffprobe: unparseable size %q", string(out))
	}
	return w, h, nil
}

func convertStillToJPEG(src, dst string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	cmd, err := videox.CommandContext(ctx, "-v", "error", "-y", "-i", src, "-q:v", "2", dst)
	if err != nil {
		return fmt.Errorf("ffmpeg not found (needed for WebP/HEIC stills)")
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("still→jpeg: %w\n%s", err, string(b))
	}
	return nil
}

// ExtractTrainingAudio dumps the clip audio next to the dataset so later
// multimodal training can use it. Best-effort: missing audio is not an error.
// startSeconds skips intro (same seek as visual bootstrap samples).
func ExtractTrainingAudio(videoPath, outputDir, samplePrefix string, startSeconds float64) (string, error) {
	audioDir := filepath.Join(outputDir, "audio")
	if err := os.MkdirAll(audioDir, 0o755); err != nil {
		return "", err
	}
	prefix := samplePrefix
	if prefix == "" {
		prefix = sanitizeStem(filepath.Base(videoPath))
	}
	out := filepath.Join(audioDir, prefix+".wav")
	args := []string{"-v", "error", "-y"}
	if startSeconds > 0 {
		args = append(args, "-ss", strconv.FormatFloat(startSeconds, 'f', 3, 64))
	}
	args = append(args, "-i", videoPath, "-vn", "-ac", "1", "-ar", "16000", out)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()
	cmd, err := videox.CommandContext(ctx, args...)
	if err != nil {
		return "", fmt.Errorf("ffmpeg nicht gefunden")
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return "", fmt.Errorf("audio-extraktion: %w\n%s", err, string(b))
	}
	return out, nil
}

func sanitizeStem(s string) string {
	var b strings.Builder
	for _, r := range s {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') {
			b.WriteRune(r)
		} else {
			b.WriteRune('_')
		}
	}
	out := b.String()
	if len(out) > 40 {
		out = out[:40]
	}
	if out == "" {
		out = "sample"
	}
	return out
}

func loadClassRegistry(dir string) (map[string]int, error) {
	path := filepath.Join(dir, "classes.json")
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]int{}, nil
		}
		return nil, err
	}
	var reg map[string]int
	if err := json.Unmarshal(data, &reg); err != nil {
		return nil, err
	}
	if reg == nil {
		reg = map[string]int{}
	}
	return reg, nil
}

// EnsureClassIDs registers each non-empty class name in classes.json
// (via bodyparts.Normalize) and returns the full name→id map. Rewrites
// data.yaml when the registry grows. Used by still bootstrap and by the
// AI Train review editor when the Owner sets any taxonomy label (not only
// the first / Face window).
func EnsureClassIDs(dir string, classNames []string) (map[string]int, error) {
	reg, err := loadClassRegistry(dir)
	if err != nil {
		return nil, err
	}
	changed := false
	for _, raw := range classNames {
		name := bodyparts.Normalize(raw)
		if name == "" {
			continue
		}
		if _, ok := lookupClassID(reg, name); ok {
			continue
		}
		_ = assignClassID(reg, name)
		changed = true
	}
	if changed {
		if err := saveClassRegistry(dir, reg); err != nil {
			return nil, err
		}
		_ = writeDataYAMLFromRegistry(dir, reg)
	}
	return reg, nil
}

// lookupClassID finds an id for a normalized class name, including legacy
// German registry keys that Normalize to the same canonical id.
func lookupClassID(reg map[string]int, canonical string) (int, bool) {
	if id, ok := reg[canonical]; ok {
		return id, true
	}
	for k, id := range reg {
		if bodyparts.Normalize(k) == canonical {
			return id, true
		}
	}
	return 0, false
}

func saveClassRegistry(dir string, reg map[string]int) error {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(reg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(dir, "classes.json"), data, 0o644)
}

func assignClassID(reg map[string]int, name string) int {
	if id, ok := reg[name]; ok {
		return id
	}
	next := 0
	for _, id := range reg {
		if id >= next {
			next = id + 1
		}
	}
	reg[name] = next
	return next
}

func writeDataYAMLFromRegistry(dir string, reg map[string]int) error {
	type pair struct {
		name string
		id   int
	}
	var names []pair
	for n, id := range reg {
		names = append(names, pair{n, id})
	}
	for i := 0; i < len(names); i++ {
		for j := i + 1; j < len(names); j++ {
			if names[j].id < names[i].id {
				names[i], names[j] = names[j], names[i]
			}
		}
	}
	var b strings.Builder
	b.WriteString("path: ")
	abs, _ := filepath.Abs(dir)
	b.WriteString(abs)
	b.WriteString("\ntrain: images/train\nval: images/val\nnames:\n")
	for _, p := range names {
		b.WriteString(fmt.Sprintf("  %d: %s\n", p.id, p.name))
	}
	return os.WriteFile(filepath.Join(dir, "data.yaml"), []byte(b.String()), 0o644)
}

func copyFile(src, dst string) error {
	in, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	return os.WriteFile(dst, in, 0o644)
}
