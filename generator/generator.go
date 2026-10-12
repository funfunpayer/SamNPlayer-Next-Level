package generator

import (
	"bufio"
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/videox"
)

//go:embed *.py requirements.txt requirements-ai.txt requirements-ai-train.txt
var pythonFiles embed.FS

//go:embed requirements.txt
var requirementsSource []byte

// command ersetzt exec.Command in diesem Paket: gleiches Verhalten, aber mit
// hiddenSysProcAttr() (siehe exec_windows.go/exec_unix.go) - ohne das blitzt
// unter Windows für jeden der vielen Python-Unterprozesse (Vorschau laden,
// Abhängigkeiten prüfen, Region suchen, Skript erzeugen, ...) kurz ein
// Konsolenfenster auf.
func command(name string, args ...string) *exec.Cmd {
	cmd := exec.Command(name, args...)
	cmd.SysProcAttr = hiddenSysProcAttr()
	return cmd
}

// commandContext is command() with a cancellable context — used by long
// generate runs so the GUI can abort without leaving an orphan Python.
func commandContext(ctx context.Context, name string, args ...string) *exec.Cmd {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.SysProcAttr = hiddenSysProcAttr()
	return cmd
}

type ROI struct {
	X, Y, W, H int
}

// ROICandidate is a ranked motion-region proposal for GUI pick-primary (TFTJ 4b).
// Never auto-applied as ROI2. Optional Class is a body-part id when a detector
// provides one (MT-Seed); empty for classical motion candidates.
type ROICandidate struct {
	X     int     `json:"x"`
	Y     int     `json:"y"`
	W     int     `json:"w"`
	H     int     `json:"h"`
	Score float64 `json:"score"`
	Index int     `json:"index"` // 1-based rank
	Class string  `json:"class,omitempty"`
}

// NamedROI is a body-part box with optional fixed flag (Tf/Tj target / mask).
type NamedROI struct {
	X     int    `json:"x"`
	Y     int    `json:"y"`
	W     int    `json:"w"`
	H     int    `json:"h"`
	Fixed bool   `json:"fixed"`
	Class string `json:"class"`
}

type Options struct {
	Invert                    bool
	SmoothWindow              int
	MinPeakDistanceMs         int
	MaxFrames                 int
	DisableCameraCompensation bool
	// UseOpenCL is retained for API compatibility; ignored. Python enables
	// OpenCL automatically when that path runs (log only). Does not force
	// Python or block the Go CSRT path.
	UseOpenCL                bool
	Threads                  int
	ReportPath               string
	Backend                  string
	Profile                  string
	PeakProminence           float64
	DynamicRangeMs           float64
	MinActionIntervalMs      float64
	MaxSpeed                 float64
	Axis                     string
	AutoRetry                bool
	AdaptiveKeyframeError    float64
	PerSceneROI              bool
	CacheDir                 string
	DisableCache             bool
	NormPercentile           float64
	RDPTolerance             float64
	DisableSceneCutDetection bool
	ROI2                     ROI
	// TipROI is the Everyday tip box (Generate x/y/w/h). Stored in
	// metadata.contact_marks.tip for Play overlay; stroke still from CSRT.
	TipROI ROI
	// ROI2Fixed keeps the second Tf/Tj box at its marked position (static
	// anchor). ROI1 is still tracked. Distance then reflects tip motion only.
	ROI2Fixed bool
	// RegionClass / RegionClass2 are optional canonical body-part IDs
	// (docs/BODY_REGIONS.md) for ROI / ROI2 — used for AI preference + UI.
	RegionClass  string
	RegionClass2 string
	// ExtraTargets are additional contact anchors beyond ROI2.
	// Distance signal = min(tip, ROI2, ExtraTargets…). Fixed=false (follow)
	// when tip path is recorded; Fixed=true for static Stay-fixed anchors.
	ExtraTargets []NamedROI
	// MaskROIs soft-exclude boxes (feature mask punch-outs); not distance drivers.
	// With RhythmGrid on the Go single-ROI path they become exclude SceneMarks
	// (M3); without RhythmGrid they still force the Python path.
	MaskROIs []ROI
	// SceneMarks are Advanced scene-map annotations (exclude/source/region)
	// honoured by the rhythm grid and camera punch-outs when RhythmGrid is on.
	SceneMarks            []SceneMark
	AIQualityOpinion      bool
	AIBaseURL             string
	ContactVibration      bool
	ContactVibrationSpan  float64
	ContactVibrationCurve string
	AudioCheck            bool
	// StartTimeSec skips the first N seconds before tracking (GUI seek past
	// black intro). 0 = start at the beginning.
	StartTimeSec float64
	// PreferPython skips the automatic Go CSRT path (CLI/tests/advanced).
	// Default false: use Go CSRT when OpenCV is linked; otherwise Python CSRT.
	PreferPython bool
	// PreferSimpletrack opts into experimental NCC (simpletrack) instead of
	// Python CSRT on builds without linked OpenCV. Lab/CLI only — not GUI.
	PreferSimpletrack bool
	// NativePipeline is retained for JSON/API compat and ignored for routing.
	NativePipeline bool
	// DetrendWindowMs / Bandpass* — FunGen/Flow-inspired post filters.
	// Bandpass 0 = off. DetrendWindowMs 0 = automatic on stroke profiles
	// (2x stroke period, see detrend_default.go), <0 = explicitly off.
	DetrendWindowMs float64
	BandpassLowHz   float64
	BandpassHighHz  float64
	// FlowDownscale shrinks frames for the optical-flow backend (e.g. 0.5).
	// 0 or 1 = full resolution. Ignored by CSRT/native.
	FlowDownscale float64
	// SkipStrokePreview disables the Stage-A sparse extrema pre-pass
	// (docs/NEXT.md § Stroke preview). Default false = run on Generate.
	SkipStrokePreview bool
	// StrokePreviewHint is filled by GenerateWithContext after the pre-pass
	// (nil if skipped/failed). Stamped into funscript metadata.
	StrokePreviewHint map[string]any
	// MaxOutputMs truncates written actions (license trial / hard clip).
	// 0 = no cap. Applied in Go finishNativeGenerate after posttrack.
	MaxOutputMs int64
	// CaptureTrajectory opts into recording raw per-frame tip/partner
	// (x,y) track positions into the script's metadata.trajectory (MT-
	// Debug Review/Play overlay). Off by default; honored on both Go CSRT
	// (trackcv) and the simpletrack (NCC) fallback, not the Python path.
	CaptureTrajectory bool
	// RhythmGrid takes the stroke signal from the most rhythmic optical-
	// flow cell near the CSRT box instead of the box's own motion - robust
	// against gradual CSRT drift on long clips (trackcv/rhythm_grid.go).
	// Opt-in; Go CSRT single-ROI stroke path only, ignored elsewhere.
	RhythmGrid bool
	// ContactPointsFile is a contact_points.py result (local teachers such
	// as NudeNet / a VLM probe). With RhythmGrid on, the grid searches around
	// those points wherever the CSRT box is out of reach (> 3 cells) - e.g.
	// the box sits on a head in a multi-person shot. Empty = off; ignored
	// without RhythmGrid. ContactPointsMinAgree keeps only points that many
	// teachers agreed on (0/1 = all).
	ContactPointsFile     string
	ContactPointsMinAgree int
	// ContactVerifyK > 0: hybrid check - the engine keeps only the contact
	// points where its own rhythm measurement is at least K times stronger
	// than at the cell it chose itself (trackcv.Options.ContactVerifyK; 1.5
	// measured). 0 = off: every point steers as before.
	ContactVerifyK float64
}

// SceneMark is an Advanced scene-map annotation passed into Generate (M3).
// Kind: exclude | source | region. Rect is pixel-space; FromMs/ToMs both 0
// means the whole clip. Follow=true tracks the box with the subject during
// Generate (exclude/source); Path is filled by the tracker for persist/learning.
// Author: "user" (painted), "suggest" (L1 priors — user must keep before Create),
// "auto" (teacher-contact candidates — review gated for P5c).
type SceneMark struct {
	Kind   string `json:"kind"`
	ID     string `json:"id"`
	Rect   ROI    `json:"rect"`
	FromMs int64  `json:"fromMs"`
	ToMs   int64  `json:"toMs"`
	// AtMs is an optional single-frame timestamp (teacher-contact candidates).
	// Pointer so omitempty round-trips through Wails/JSON without a false 0.
	AtMs   *int64 `json:"atMs,omitempty"`
	Class  string `json:"class,omitempty"`
	Author string `json:"author,omitempty"`
	// Confidence + Reviewed are M5 training-gate fields (funscript.SceneMapMark).
	// Persist/load must round-trip them so P5c can see reviewed:true auto marks
	// after a GUI companion restore — not only from raw .samn export.
	Confidence float64      `json:"confidence,omitempty"`
	Reviewed   *bool        `json:"reviewed,omitempty"`
	Follow     bool         `json:"follow,omitempty"`
	Path       []MarkSample `json:"path,omitempty"`
}

// MarkSample is one Follow-path box at time Ms (learning + engine).
type MarkSample struct {
	Ms   int64 `json:"ms"`
	Rect ROI   `json:"rect"`
}

func pythonCandidates() []string {
	var out []string
	seen := map[string]bool{}
	add := func(p string) {
		if p == "" || seen[p] {
			return
		}
		seen[p] = true
		out = append(out, p)
	}
	for _, name := range []string{"python3", "python", "py"} {
		if path, err := exec.LookPath(name); err == nil {
			add(path)
		}
	}
	var patterns []string
	if local := os.Getenv("LOCALAPPDATA"); local != "" {
		patterns = append(patterns, filepath.Join(local, "Programs", "Python", "Python3*", "python.exe"))
	}
	if pf := os.Getenv("ProgramFiles"); pf != "" {
		patterns = append(patterns, filepath.Join(pf, "Python3*", "python.exe"))
	}
	patterns = append(patterns, `C:\Python3*\python.exe`)
	for _, pattern := range patterns {
		matches, _ := filepath.Glob(pattern)
		for i := len(matches) - 1; i >= 0; i-- {
			add(matches[i])
		}
	}
	// Windows Store stubs (%LOCALAPPDATA%\Microsoft\WindowsApps\python*.exe)
	// often sit first on PATH but are not the install users pip into. Prefer
	// real interpreters when any exist (Issues #94/#119).
	return demoteWindowsAppsStubs(out)
}

func isWindowsAppsPythonStub(path string) bool {
	// Normalize both \ and / so detection works when unit tests pass
	// Windows paths on a Linux builder (filepath.ToSlash alone does not).
	lower := strings.ToLower(strings.ReplaceAll(path, `\`, "/"))
	return strings.Contains(lower, "/windowsapps/")
}

// demoteWindowsAppsStubs moves WindowsApps alias stubs to the end so
// FindPython / CheckDependencies prefer a real install (e.g.
// %LOCALAPPDATA%\Programs\Python\…) when both appear on PATH.
func demoteWindowsAppsStubs(paths []string) []string {
	var preferred, stubs []string
	for _, p := range paths {
		if isWindowsAppsPythonStub(p) {
			stubs = append(stubs, p)
			continue
		}
		preferred = append(preferred, p)
	}
	if len(preferred) == 0 {
		return paths
	}
	return append(preferred, stubs...)
}

func hasPackages(py string) (bool, string) {
	// CSRT must exist — plain opencv-python (no contrib) imports cv2
	// but leaves create_tracker() dead (Issues #94/#95). KCF/MIL alone
	// is not accepted (no silent weak-tracker Ausfallcode).
	script := "import cv2, scipy, numpy\n" +
		"ok=False\n" +
		"leg=getattr(cv2,'legacy',None)\n" +
		"for free,cls in (('TrackerCSRT_create','TrackerCSRT'),):\n" +
		"  if callable(getattr(cv2,free,None)) or (getattr(cv2,cls,None) is not None and callable(getattr(getattr(cv2,cls,None),'create',None))):\n" +
		"    ok=True; break\n" +
		"  if leg is not None and (callable(getattr(leg,free,None)) or (getattr(leg,cls,None) is not None and callable(getattr(getattr(leg,cls,None),'create',None)))):\n" +
		"    ok=True; break\n" +
		"assert ok, 'OpenCV ohne CSRT — oft opencv-python statt opencv-contrib-python. pip uninstall opencv-python opencv-python-headless && pip install opencv-contrib-python'\n"
	out, err := command(py, "-c", script).CombinedOutput()
	return err == nil, string(out)
}

// pythonHasCSRT reports whether the given interpreter's cv2 exposes any CSRT
// factory (legacy or main). Used for a pre-track check when Generate must
// take the Python path (#338) — hasPackages requires CSRT only.
func pythonHasCSRT(py string) (bool, string) {
	script := "import cv2\n" +
		"leg=getattr(cv2,'legacy',None)\n" +
		"ok=False\n" +
		"if callable(getattr(cv2,'TrackerCSRT_create',None)): ok=True\n" +
		"cls=getattr(cv2,'TrackerCSRT',None)\n" +
		"if cls is not None and callable(getattr(cls,'create',None)): ok=True\n" +
		"if leg is not None:\n" +
		"  if callable(getattr(leg,'TrackerCSRT_create',None)): ok=True\n" +
		"  lcls=getattr(leg,'TrackerCSRT',None)\n" +
		"  if lcls is not None and callable(getattr(lcls,'create',None)): ok=True\n" +
		"ver=getattr(cv2,'__version__','?')\n" +
		"print(('CSRT ok' if ok else 'no CSRT')+' in OpenCV '+ver)\n" +
		"raise SystemExit(0 if ok else 1)\n"
	out, err := command(py, "-c", script).CombinedOutput()
	detail := strings.TrimSpace(firstLine(string(out)))
	if detail == "" {
		detail = "unknown OpenCV"
	}
	return err == nil, detail
}

func FindPython() (string, error) {
	candidates := pythonCandidates()
	if len(candidates) == 0 {
		return "", fmt.Errorf("generator: no Python found (python3/python/py on PATH) — install Python 3.9+: https://python.org")
	}
	var firstRunnable string
	for _, py := range candidates {
		ok, _ := hasPackages(py)
		if ok {
			return py, nil
		}
		if firstRunnable == "" {
			if err := command(py, "-c", "pass").Run(); err == nil {
				firstRunnable = py
			}
		}
	}
	if firstRunnable != "" {
		return firstRunnable, nil
	}
	return "", fmt.Errorf("generator: no working Python interpreter found (checked: %v)", candidates)
}

func CheckDependencies() error {
	candidates := pythonCandidates()
	if len(candidates) == 0 {
		_, err := FindPython()
		return err
	}
	var details []string
	for _, py := range candidates {
		ok, out := hasPackages(py)
		if ok {
			return nil
		}
		details = append(details, fmt.Sprintf("  %s: %s", py, firstLine(out)))
	}
	target, err := FindPython()
	if err != nil {
		return err
	}
	return fmt.Errorf("generator: missing packages. Install with: \"%s\" -m pip install opencv-contrib-python scipy numpy\n(Important: do not install alongside opencv-python — it removes CSRT.)\nChecked:\n%s", target, strings.Join(details, "\n"))
}

func firstLine(s string) string {
	for _, line := range strings.Split(s, "\n") {
		line = strings.TrimSpace(line)
		if line != "" && !strings.HasPrefix(line, "Traceback") && !strings.HasPrefix(line, "File \"") {
			return line
		}
	}
	return "failed to start"
}

func writeScriptToTemp() (string, error) {
	dir, err := os.MkdirTemp("", "funscript-generator-*")
	if err != nil {
		return "", fmt.Errorf("generator: Temp-Verzeichnis: %w", err)
	}
	entries, err := pythonFiles.ReadDir(".")
	if err != nil {
		os.RemoveAll(dir)
		return "", fmt.Errorf("generator: embedded files not readable: %w", err)
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if strings.HasSuffix(name, "_test.py") {
			continue
		}
		content, err := pythonFiles.ReadFile(name)
		if err != nil {
			os.RemoveAll(dir)
			return "", fmt.Errorf("generator: %s not readable: %w", name, err)
		}
		if err := os.WriteFile(filepath.Join(dir, name), content, 0644); err != nil {
			os.RemoveAll(dir)
			return "", fmt.Errorf("generator: %s could not be written: %w", name, err)
		}
	}
	return filepath.Join(dir, "generate_funscript.py"), nil
}

func cleanupScriptTemp(scriptPath string) {
	os.RemoveAll(filepath.Dir(scriptPath))
}

func writeEmbeddedToTemp(pattern string, content []byte) (string, error) {
	f, err := os.CreateTemp("", pattern)
	if err != nil {
		return "", fmt.Errorf("generator: temp file for script: %w", err)
	}
	defer f.Close()
	if _, err := f.Write(content); err != nil {
		os.Remove(f.Name())
		return "", fmt.Errorf("generator: script could not be written: %w", err)
	}
	return f.Name(), nil
}

func parseProgress(line string) (done, total int, ok bool) {
	if !strings.HasPrefix(line, "PROGRESS ") {
		return 0, 0, false
	}
	if n, _ := fmt.Sscanf(line, "PROGRESS %d %d", &done, &total); n != 2 {
		return 0, 0, false
	}
	return done, total, true
}

func DefaultCacheDir() string {
	dir, err := os.UserCacheDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "SamNPlayer")
}

func percentOf(done, total int) int {
	if total <= 0 {
		return -1
	}
	pct := done * 100 / total
	if pct > 100 {
		return 100
	}
	if pct < 0 {
		return 0
	}
	return pct
}

func FindROI(videoPath string, onProgress func(line string)) (ROI, error) {
	return FindROIWithProgress(videoPath, onProgress, nil)
}

func FindROIWithProgress(videoPath string, onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	return FindROIWithContext(context.Background(), videoPath, onProgress, onPercent)
}

// FindROIWithContext is FindROIWithProgress with cancel via ctx (CommandContext).
func FindROIWithContext(ctx context.Context, videoPath string, onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	return findROIViaScriptCtx(ctx, "auto_roi.py", nil, videoPath, "auto_roi", onProgress, onPercent)
}

// FindROICandidatesWithProgress lists ranked motion regions (auto_roi --list).
// Read-only proposals — caller must not silently commit ROI2 (TFTJ step 4b;
// only ApplySceneProposal under the opt-in "Apply AI setup automatically").
func FindROICandidatesWithProgress(videoPath string, onProgress func(line string), onPercent func(pct int)) ([]ROICandidate, error) {
	return FindROICandidatesWithContext(context.Background(), videoPath, onProgress, onPercent)
}

// FindROICandidatesWithContext is FindROICandidatesWithProgress with cancel via ctx.
func FindROICandidatesWithContext(ctx context.Context, videoPath string, onProgress func(line string), onPercent func(pct int)) ([]ROICandidate, error) {
	return findROICandidatesViaScriptCtx(ctx, videoPath, onProgress, onPercent)
}

// FindTwoROIsWithProgress schlägt ROI1+ROI2 vor (auto_roi --two). Nur
// Vorschlag — GUI muss bestätigen/korrigieren (docs/NEXT.md Priorität 3).
func FindTwoROIsWithProgress(videoPath string, onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	return FindTwoROIsWithContext(context.Background(), videoPath, onProgress, onPercent)
}

// FindTwoROIsWithContext is FindTwoROIsWithProgress with cancel via ctx.
func FindTwoROIsWithContext(ctx context.Context, videoPath string, onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	return findTwoROIsViaScriptCtx(ctx, "auto_roi.py", []string{"--two"}, videoPath, "auto_roi", onProgress, onPercent)
}

// FindROIAIWithProgress is the AI variant of FindROIWithProgress: same
// stdout/stderr contract, but ai_roi.py (local ONNX) instead of auto_roi.py.
// modelPath == "" uses ai_roi.default_model_path(). preferredClasses is a
// comma-separated list of names or ids (empty = no class filter).
func FindROIAIWithProgress(videoPath, modelPath, preferredClasses string, onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	return FindROIAIWithContext(context.Background(), videoPath, modelPath, preferredClasses, onProgress, onPercent)
}

// FindROIAIWithContext is FindROIAIWithProgress with cancel via ctx.
func FindROIAIWithContext(ctx context.Context, videoPath, modelPath, preferredClasses string, onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	var extraArgs []string
	if modelPath != "" {
		extraArgs = append(extraArgs, "--model", modelPath)
	}
	if preferredClasses != "" {
		extraArgs = append(extraArgs, "--preferred-classes", preferredClasses)
		if modelPath != "" {
			extraArgs = append(extraArgs, "--classes-json", filepath.Dir(modelPath))
		}
	}
	return findROIViaScriptCtx(ctx, "ai_roi.py", extraArgs, videoPath, "ai_roi", onProgress, onPercent)
}

// AIDetection is one labeled ONNX box (multi-class list — not tip-only).
type AIDetection struct {
	X          int     `json:"x"`
	Y          int     `json:"y"`
	W          int     `json:"w"`
	H          int     `json:"h"`
	Confidence float64 `json:"confidence"`
	ClassID    int     `json:"classId"`
	ClassName  string  `json:"className"`
	Index      int     `json:"index"`
}

// ListAIDetectionsWithProgress runs ai_roi.py --list-detections on one frame.
// Returns every class above confidence (nipples/breasts/glans/…), not a single tip.
func ListAIDetectionsWithProgress(videoPath, modelPath, preferredClasses string, timeSec float64,
	onProgress func(line string), onPercent func(pct int)) ([]AIDetection, error) {
	return ListAIDetectionsWithContext(context.Background(), videoPath, modelPath, preferredClasses, timeSec, onProgress, onPercent)
}

// ListAIDetectionsWithContext is ListAIDetectionsWithProgress with cancel via ctx.
func ListAIDetectionsWithContext(ctx context.Context, videoPath, modelPath, preferredClasses string, timeSec float64,
	onProgress func(line string), onPercent func(pct int)) ([]AIDetection, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	py, err := FindPython()
	if err != nil {
		return nil, err
	}
	if err := CheckDependencies(); err != nil {
		return nil, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return nil, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "ai_roi.py")
	args := []string{scriptPath, "--video", videoPath, "--list-detections",
		"--time-sec", fmt.Sprintf("%.3f", timeSec)}
	if modelPath != "" {
		args = append(args, "--model", modelPath, "--classes-json", filepath.Dir(modelPath))
	}
	if preferredClasses != "" {
		args = append(args, "--preferred-classes", preferredClasses)
	}
	cmd := commandContext(ctx, py, args...)
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("generator: stdout-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("generator: start failed: %w", err)
	}
	var stderrDone sync.WaitGroup
	stderrDone.Add(1)
	go func() {
		defer stderrDone.Done()
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := sc.Text()
			if done, total, ok := parseProgress(line); ok {
				if onPercent != nil {
					onPercent(percentOf(done, total))
				}
				continue
			}
			logging.Debug("ai_roi list: " + line)
			if onProgress != nil {
				onProgress(line)
			}
		}
	}()
	var out []AIDetection
	sc := bufio.NewScanner(stdout)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if !strings.HasPrefix(line, "DETECTION ") {
			continue
		}
		fields := strings.Fields(line)
		// DETECTION x y w h conf classId className index
		if len(fields) < 9 {
			continue
		}
		x, e1 := strconv.Atoi(fields[1])
		y, e2 := strconv.Atoi(fields[2])
		w, e3 := strconv.Atoi(fields[3])
		h, e4 := strconv.Atoi(fields[4])
		conf, e5 := strconv.ParseFloat(fields[5], 64)
		cid, e6 := strconv.Atoi(fields[6])
		idx, e7 := strconv.Atoi(fields[len(fields)-1])
		if e1 != nil || e2 != nil || e3 != nil || e4 != nil || e5 != nil || e6 != nil || e7 != nil {
			continue
		}
		name := strings.Join(fields[7:len(fields)-1], "_")
		out = append(out, AIDetection{
			X: x, Y: y, W: w, H: h, Confidence: conf,
			ClassID: cid, ClassName: name, Index: idx,
		})
	}
	stderrDone.Wait()
	if err := cmd.Wait(); err != nil {
		if ctx.Err() != nil {
			return out, fmt.Errorf("generator: ai_roi --list-detections cancelled: %w", ctx.Err())
		}
		return out, fmt.Errorf("generator: ai_roi --list-detections: %w", err)
	}
	return out, nil
}

// TipDetection is a semantic AI proposal. It is deliberately separate from
// the generic auto-ROI result: Match must be true and the frontend must still
// ask the user to Apply before copying this box into the generator ROI.
type TipDetection struct {
	X              int     `json:"x"`
	Y              int     `json:"y"`
	W              int     `json:"w"`
	H              int     `json:"h"`
	ExpectedClass  string  `json:"expectedClass"`
	MatchedClass   string  `json:"matchedClass"`
	MatchedClassID int     `json:"matchedClassId"`
	Confidence     float64 `json:"confidence"`
	SampleIndex    int     `json:"sampleIndex"`
	Match          bool    `json:"match"`
	Status         string  `json:"status"`
}

// StrictTipDetectionError carries a stable reason code from ai_roi.py. A
// strict failure is a normal "mark manually" outcome and never triggers the
// generic motion finder or a different body class.
type StrictTipDetectionError struct {
	Code          string `json:"code"`
	ExpectedClass string `json:"expectedClass"`
	Message       string `json:"message"`
	Match         bool   `json:"match"`
}

func (e *StrictTipDetectionError) Error() string {
	if e.Message != "" {
		return e.Message
	}
	return "strict AI target detection failed: " + e.Code
}

func expectedTipAIArgs(modelPath, expectedClass string, timeSec float64) []string {
	args := []string{
		"--expected-class", expectedClass,
		"--strict-class",
		"--time-sec", strconv.FormatFloat(max(0, timeSec), 'f', 3, 64),
	}
	if modelPath != "" {
		args = append(args, "--model", modelPath, "--classes-json", filepath.Dir(modelPath))
	}
	return args
}

func parseTipDetectionLine(line string) (TipDetection, *StrictTipDetectionError, bool) {
	if raw, ok := strings.CutPrefix(line, "TIP_DETECTION "); ok {
		var result TipDetection
		if json.Unmarshal([]byte(raw), &result) == nil {
			return result, nil, true
		}
	}
	if raw, ok := strings.CutPrefix(line, "TIP_ERROR "); ok {
		var result StrictTipDetectionError
		if json.Unmarshal([]byte(raw), &result) == nil {
			return TipDetection{}, &result, true
		}
	}
	return TipDetection{}, nil, false
}

// FindExpectedTipROIAIWithProgress asks the local ONNX detector for exactly
// expectedClass. The model-sibling classes.json is mandatory in Python strict
// mode. No preferred-class or motion fallback is used here.
func FindExpectedTipROIAIWithProgress(videoPath, modelPath, expectedClass string,
	onProgress func(line string), onPercent func(pct int)) (TipDetection, error) {
	return FindExpectedTipROIAIWithProgressContext(
		context.Background(), videoPath, modelPath, expectedClass, 0, onProgress, onPercent)
}

// FindExpectedTipROIAIWithProgressContext is the cancellable variant used by
// Wails so a class/video change terminates superseded local inference work.
func FindExpectedTipROIAIWithProgressContext(ctx context.Context,
	videoPath, modelPath, expectedClass string, timeSec float64,
	onProgress func(line string), onPercent func(pct int)) (TipDetection, error) {
	return findExpectedTipROIAIWithProgressContext(
		ctx, []string{"--video", videoPath}, modelPath, expectedClass, timeSec,
		onProgress, onPercent)
}

// FindExpectedTipImageAIWithProgressContext evaluates the exact image bytes
// already extracted for the GUI preview, avoiding a second decoder/seek path.
func FindExpectedTipImageAIWithProgressContext(ctx context.Context,
	imagePath, modelPath, expectedClass string,
	onProgress func(line string), onPercent func(pct int)) (TipDetection, error) {
	return findExpectedTipROIAIWithProgressContext(
		ctx, []string{"--image", imagePath}, modelPath, expectedClass, 0,
		onProgress, onPercent)
}

func findExpectedTipROIAIWithProgressContext(ctx context.Context, sourceArgs []string,
	modelPath, expectedClass string, timeSec float64,
	onProgress func(line string), onPercent func(pct int)) (TipDetection, error) {
	// Honor cancel before dependency/Python work so UI races and unit tests
	// do not require cv2 just to prove the context short-circuit.
	if err := ctx.Err(); err != nil {
		return TipDetection{}, err
	}
	if strings.TrimSpace(expectedClass) == "" {
		return TipDetection{}, &StrictTipDetectionError{
			Code: "class_unresolved", Message: "expected body class is required",
		}
	}
	py, err := FindPython()
	if err != nil {
		return TipDetection{}, err
	}
	if err := CheckDependencies(); err != nil {
		return TipDetection{}, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return TipDetection{}, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "ai_roi.py")
	args := append([]string{scriptPath}, sourceArgs...)
	args = append(args, expectedTipAIArgs(modelPath, expectedClass, timeSec)...)
	cmd := commandContext(ctx, py, args...)
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return TipDetection{}, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return TipDetection{}, fmt.Errorf("generator: stdout-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return TipDetection{}, fmt.Errorf("generator: strict AI target start failed: %w", err)
	}
	var stderrDone sync.WaitGroup
	stderrDone.Add(1)
	go func() {
		defer stderrDone.Done()
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := sc.Text()
			if done, total, ok := parseProgress(line); ok {
				if onPercent != nil {
					onPercent(percentOf(done, total))
				}
				continue
			}
			logging.Debug("ai_roi strict: " + line)
			if onProgress != nil {
				onProgress(line)
			}
		}
	}()
	var detection TipDetection
	var strictErr *StrictTipDetectionError
	sc := bufio.NewScanner(stdout)
	for sc.Scan() {
		result, typedErr, ok := parseTipDetectionLine(sc.Text())
		if !ok {
			continue
		}
		if typedErr != nil {
			strictErr = typedErr
		} else {
			detection = result
		}
	}
	stderrDone.Wait()
	waitErr := cmd.Wait()
	if strictErr != nil {
		return TipDetection{}, strictErr
	}
	if errors.Is(ctx.Err(), context.Canceled) {
		return TipDetection{}, context.Canceled
	}
	if waitErr != nil {
		return TipDetection{}, fmt.Errorf("generator: strict AI target failed: %w", waitErr)
	}
	if !detection.Match || detection.W <= 0 || detection.H <= 0 {
		return TipDetection{}, &StrictTipDetectionError{
			Code: "target_not_detected", ExpectedClass: expectedClass,
			Message: "strict AI target returned no usable matched box",
		}
	}
	return detection, nil
}

// FindTwoROIsAIWithProgress is the AI variant of FindTwoROIsWithProgress
// (ai_roi.py --two). roi2 may be empty when no second object was found.
func FindTwoROIsAIWithProgress(videoPath, modelPath, preferredClasses string, onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	return FindTwoROIsAIWithContext(context.Background(), videoPath, modelPath, preferredClasses, onProgress, onPercent)
}

// FindTwoROIsAIWithContext is FindTwoROIsAIWithProgress with cancel via ctx.
func FindTwoROIsAIWithContext(ctx context.Context, videoPath, modelPath, preferredClasses string, onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	extraArgs := []string{"--two"}
	if modelPath != "" {
		extraArgs = append(extraArgs, "--model", modelPath)
	}
	if preferredClasses != "" {
		extraArgs = append(extraArgs, "--preferred-classes", preferredClasses)
		if modelPath != "" {
			extraArgs = append(extraArgs, "--classes-json", filepath.Dir(modelPath))
		}
	}
	return findTwoROIsViaScriptCtx(ctx, "ai_roi.py", extraArgs, videoPath, "ai_roi", onProgress, onPercent)
}

// AIRoiAvailable prüft (ohne ein Video zu öffnen), ob die KI-Regionssuche
// grundsätzlich nutzbar ist - onnxruntime installiert UND ein Modell unter
// modelPath (oder ai_roi.py's Standardordner) vorhanden. Für die GUI, um den
// KI-Knopf zu aktivieren/auszublenden. Liefert false bei jedem Fehler
// (fehlendes Python, fehlende Pakete, ...) statt den Fehler durchzureichen -
// die Verfügbarkeitsprüfung soll nie selbst scheitern können.
func AIRoiAvailable(modelPath string) bool {
	py, err := FindPython()
	if err != nil {
		return false
	}
	if err := CheckDependencies(); err != nil {
		return false
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return false
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "ai_roi.py")
	args := []string{scriptPath, "--check"}
	if modelPath != "" {
		args = append(args, "--model", modelPath)
	}
	out, err := command(py, args...).Output()
	if err != nil {
		return false
	}
	return strings.TrimSpace(string(out)) == "AVAILABLE"
}

// SupportSignalsAvailable reports experimental depth/pose helper availability
// (classical proxy always on; ONNX flags only when optional models exist).
// Returns nil on any probe failure so callers can treat it as unavailable.
func SupportSignalsAvailable(depthOnnxPath, poseOnnxPath string) map[string]bool {
	py, err := FindPython()
	if err != nil {
		return nil
	}
	if err := CheckDependencies(); err != nil {
		return nil
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return nil
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "support_signals.py")
	args := []string{scriptPath, "--check"}
	if depthOnnxPath != "" {
		args = append(args, "--depth-onnx", depthOnnxPath)
	}
	if poseOnnxPath != "" {
		args = append(args, "--pose-onnx", poseOnnxPath)
	}
	out, err := command(py, args...).Output()
	if err != nil {
		return nil
	}
	var flags map[string]bool
	if err := json.Unmarshal(out, &flags); err != nil {
		return nil
	}
	return flags
}

// AudioCheckAvailable prüft, ob --audio-check grundsätzlich nutzbar ist -
// nur ffmpeg auf dem PATH nötig (Go: CheckAudioTempo / Python: audio_check.py).
// Kein separates Python-Paket. Für die GUI, um die Checkbox zu aktivieren/
// auszublenden statt sie anzubieten und dann bei jedem Versuch mit
// "nicht möglich" scheitern zu lassen.
func AudioCheckAvailable() bool {
	return videox.Available()
}

// findROIViaScript führt eines der beiden austauschbaren ROI-Finder-Skripte
// (auto_roi.py, ai_roi.py) aus - beide erfüllen denselben Vertrag: stdout
// "ROI x y w h", stderr optional "PROGRESS done total"-Zeilen plus Logtext.
// logPrefix kennzeichnet nur die Logzeilen, ändert das Protokoll nicht.
func findROIViaScript(scriptName string, extraArgs []string, videoPath, logPrefix string,
	onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	return findROIViaScriptCtx(context.Background(), scriptName, extraArgs, videoPath, logPrefix, onProgress, onPercent)
}

func findROIViaScriptCtx(ctx context.Context, scriptName string, extraArgs []string, videoPath, logPrefix string,
	onProgress func(line string), onPercent func(pct int)) (ROI, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	py, err := FindPython()
	if err != nil {
		return ROI{}, err
	}
	if err := CheckDependencies(); err != nil {
		return ROI{}, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return ROI{}, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), scriptName)
	args := append([]string{scriptPath, "--video", videoPath}, extraArgs...)
	cmd := commandContext(ctx, py, args...)
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return ROI{}, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return ROI{}, fmt.Errorf("generator: stdout-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return ROI{}, fmt.Errorf("generator: start failed: %w", err)
	}
	// cmd.Wait() schließt die Pipes, sobald der Prozess beendet ist - laut
	// os/exec-Doku "incorrect to call Wait before all reads from the pipe
	// have completed". stdout enthält hier nur eine kurze "ROI ..."-Zeile
	// und ist meist sofort fertig gelesen, während stderr (Fortschritt/Log,
	// oft viel mehr Text) noch in der Goroutine läuft - ohne WaitGroup
	// konnte Wait() die stderr-Pipe schließen, bevor die Goroutine
	// überhaupt zu lesen begonnen hatte, und praktisch der gesamte
	// Fortschritt/Log ging verloren (reproduziert: 0 von 20000 Testzeilen
	// empfangen).
	var stderrDone sync.WaitGroup
	stderrDone.Add(1)
	go func() {
		defer stderrDone.Done()
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := sc.Text()
			if done, total, ok := parseProgress(line); ok {
				if onPercent != nil {
					onPercent(percentOf(done, total))
				}
				continue
			}
			logging.Debug(logPrefix + ": " + line)
			if onProgress != nil {
				onProgress(line)
			}
		}
	}()
	var roi ROI
	found := false
	sc := bufio.NewScanner(stdout)
	for sc.Scan() {
		var x, y, w, h int
		if n, _ := fmt.Sscanf(sc.Text(), "ROI %d %d %d %d", &x, &y, &w, &h); n == 4 {
			roi = ROI{X: x, Y: y, W: w, H: h}
			found = true
		}
	}
	stderrDone.Wait()
	if err := cmd.Wait(); err != nil {
		if ctx.Err() != nil {
			return ROI{}, fmt.Errorf("generator: automatic region search cancelled: %w", ctx.Err())
		}
		return ROI{}, fmt.Errorf("generator: automatic region search failed: %w", err)
	}
	if !found {
		return ROI{}, fmt.Errorf("generator: no region found — mark manually")
	}
	logging.Info("generator: region found automatically", "roi", fmt.Sprintf("%+v", roi))
	return roi, nil
}

// findROICandidatesViaScript runs auto_roi.py --list and parses CANDIDATE lines.
func findROICandidatesViaScript(videoPath string, onProgress func(line string), onPercent func(pct int)) ([]ROICandidate, error) {
	return findROICandidatesViaScriptCtx(context.Background(), videoPath, onProgress, onPercent)
}

func findROICandidatesViaScriptCtx(ctx context.Context, videoPath string, onProgress func(line string), onPercent func(pct int)) ([]ROICandidate, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	py, err := FindPython()
	if err != nil {
		return nil, err
	}
	if err := CheckDependencies(); err != nil {
		return nil, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return nil, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "auto_roi.py")
	cmd := commandContext(ctx, py, scriptPath, "--video", videoPath, "--list")
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("generator: stdout-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("generator: start failed: %w", err)
	}
	var stderrDone sync.WaitGroup
	stderrDone.Add(1)
	go func() {
		defer stderrDone.Done()
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := sc.Text()
			if done, total, ok := parseProgress(line); ok {
				if onPercent != nil {
					onPercent(percentOf(done, total))
				}
				continue
			}
			logging.Debug("auto_roi: " + line)
			if onProgress != nil {
				onProgress(line)
			}
		}
	}()
	var out []ROICandidate
	sc := bufio.NewScanner(stdout)
	for sc.Scan() {
		var idx, x, y, w, h int
		var score float64
		line := sc.Text()
		if n, _ := fmt.Sscanf(line, "CANDIDATE %d %d %d %d %d %f", &idx, &x, &y, &w, &h, &score); n == 6 {
			out = append(out, ROICandidate{X: x, Y: y, W: w, H: h, Score: score, Index: idx})
		}
	}
	stderrDone.Wait()
	if err := cmd.Wait(); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("generator: motion candidate search cancelled: %w", ctx.Err())
		}
		return nil, fmt.Errorf("generator: motion candidate search failed: %w", err)
	}
	if len(out) == 0 {
		return nil, fmt.Errorf("generator: no motion candidates — mark manually")
	}
	logging.Info("generator: motion candidates found", "count", len(out))
	return out, nil
}

// findTwoROIsViaScript wie findROIViaScript, liest zusätzlich optional
// "ROI2 x y w h". Fehlt ROI2, ist der zweite Rückgabewert leer (W=0).
func findTwoROIsViaScript(scriptName string, extraArgs []string, videoPath, logPrefix string,
	onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	return findTwoROIsViaScriptCtx(context.Background(), scriptName, extraArgs, videoPath, logPrefix, onProgress, onPercent)
}

func findTwoROIsViaScriptCtx(ctx context.Context, scriptName string, extraArgs []string, videoPath, logPrefix string,
	onProgress func(line string), onPercent func(pct int)) (ROI, ROI, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	py, err := FindPython()
	if err != nil {
		return ROI{}, ROI{}, err
	}
	if err := CheckDependencies(); err != nil {
		return ROI{}, ROI{}, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return ROI{}, ROI{}, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), scriptName)
	args := append([]string{scriptPath, "--video", videoPath}, extraArgs...)
	cmd := commandContext(ctx, py, args...)
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return ROI{}, ROI{}, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return ROI{}, ROI{}, fmt.Errorf("generator: stdout-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return ROI{}, ROI{}, fmt.Errorf("generator: start failed: %w", err)
	}
	var stderrDone sync.WaitGroup
	stderrDone.Add(1)
	go func() {
		defer stderrDone.Done()
		sc := bufio.NewScanner(stderr)
		for sc.Scan() {
			line := sc.Text()
			if done, total, ok := parseProgress(line); ok {
				if onPercent != nil {
					onPercent(percentOf(done, total))
				}
				continue
			}
			logging.Debug(logPrefix + ": " + line)
			if onProgress != nil {
				onProgress(line)
			}
		}
	}()
	var roi, roi2 ROI
	found := false
	sc := bufio.NewScanner(stdout)
	for sc.Scan() {
		line := sc.Text()
		var x, y, w, h int
		if n, _ := fmt.Sscanf(line, "ROI2 %d %d %d %d", &x, &y, &w, &h); n == 4 {
			roi2 = ROI{X: x, Y: y, W: w, H: h}
			continue
		}
		if n, _ := fmt.Sscanf(line, "ROI %d %d %d %d", &x, &y, &w, &h); n == 4 {
			roi = ROI{X: x, Y: y, W: w, H: h}
			found = true
		}
	}
	stderrDone.Wait()
	if err := cmd.Wait(); err != nil {
		if ctx.Err() != nil {
			return ROI{}, ROI{}, fmt.Errorf("generator: automatic two-region search cancelled: %w", ctx.Err())
		}
		return ROI{}, ROI{}, fmt.Errorf("generator: automatic two-region search failed: %w", err)
	}
	if !found {
		return ROI{}, ROI{}, fmt.Errorf("generator: no region found — mark manually")
	}
	logging.Info("generator: regions found automatically",
		"roi", fmt.Sprintf("%+v", roi), "roi2", fmt.Sprintf("%+v", roi2))
	return roi, roi2, nil
}

func DumpFirstFrame(videoPath, outputPNG string) (width, height int, err error) {
	py, err := FindPython()
	if err != nil {
		return 0, 0, err
	}
	if err := CheckDependencies(); err != nil {
		return 0, 0, err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return 0, 0, err
	}
	defer cleanupScriptTemp(scriptPath)
	cmd := command(py, scriptPath, "--video", videoPath, "--dump-first-frame", outputPNG)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return 0, 0, fmt.Errorf("generator: frame extraction failed: %w\n%s", err, string(out))
	}
	for _, line := range splitLines(string(out)) {
		var w, h int
		if n, _ := fmt.Sscanf(line, "FRAME_SIZE %d %d", &w, &h); n == 2 {
			return w, h, nil
		}
	}
	return 0, 0, fmt.Errorf("generator: frame size not readable from script output: %s", string(out))
}

// ProfileSuggestion ist das Ergebnis von SuggestProfile. Found=false ist ein
// normales Ergebnis (keine gespeicherte Szene nah genug, kein KI-Server
// erreichbar), kein Fehler - der Aufrufer entscheidet, was er anzeigt.
type ProfileSuggestion struct {
	Found bool   `json:"found"`
	Label string `json:"label"`
	Kind  string `json:"kind"` // "measured", "local_model" oder "ai"
	// Confidence: bei Kind=="ai" eine 0..1-Konfidenz (höher = sicherer).
	// Bei Kind=="measured" stattdessen der Signaturabstand zur nächsten
	// gespeicherten Szene (niedriger = ähnlicher) - andere Skala, gleiches
	// Feld, weil beide Fälle nie gleichzeitig auftreten.
	Confidence float64 `json:"confidence"`
}

// ExtractMotionSignature asks the existing OpenCV feature extractor for its
// compact, versioned scene signature. Video decoding remains in the proven
// Python/OpenCV path; model training and inference can consume the result in
// pure Go without importing Python ML frameworks.
func ExtractMotionSignature(videoPath string) (map[string]float64, error) {
	py, err := FindPython()
	if err != nil {
		return nil, err
	}
	if err := CheckDependencies(); err != nil {
		return nil, err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return nil, err
	}
	defer cleanupScriptTemp(scriptPath)
	out, err := command(py, scriptPath, "--video", videoPath, "--dump-motion-signature").Output()
	if err != nil {
		return nil, fmt.Errorf("generator: motion signature failed: %w", err)
	}
	var signature map[string]float64
	if err := json.Unmarshal(out, &signature); err != nil {
		return nil, fmt.Errorf("generator: motion signature output invalid: %w", err)
	}
	return signature, nil
}

// SuggestProfile fragt --suggest-profile ab (generate_funscript.py, siehe
// motion_signature.py/ai_profile.py): vergleicht die Bewegungssignatur des
// Videos zuerst gegen mit LabelScene benannte Szenen (gemessen, keine KI),
// und nur wenn keine nah genug ist, gegen einen optionalen Colibri-Server.
// baseURL == "" nutzt colibri_client.DEFAULT_BASE_URL.
func SuggestProfile(videoPath, baseURL string) (ProfileSuggestion, error) {
	py, err := FindPython()
	if err != nil {
		return ProfileSuggestion{}, err
	}
	if err := CheckDependencies(); err != nil {
		return ProfileSuggestion{}, err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return ProfileSuggestion{}, err
	}
	defer cleanupScriptTemp(scriptPath)
	args := []string{scriptPath, "--video", videoPath, "--suggest-profile"}
	if baseURL != "" {
		args = append(args, "--ai-base-url", baseURL)
	}
	out, err := command(py, args...).Output()
	if err != nil {
		return ProfileSuggestion{}, fmt.Errorf("generator: profile suggestion failed: %w", err)
	}
	// "PROFILE_SUGGESTION <label> <measured|ai> <confidence>" - Label kann
	// selbst Leerzeichen enthalten (freier Szenenname), darum von den festen
	// letzten beiden Feldern her parsen statt von vorne zu zählen.
	for _, line := range splitLines(string(out)) {
		fields := strings.Fields(line)
		if len(fields) < 4 || fields[0] != "PROFILE_SUGGESTION" {
			continue
		}
		confidence, _ := strconv.ParseFloat(fields[len(fields)-1], 64)
		return ProfileSuggestion{
			Found:      true,
			Label:      strings.Join(fields[1:len(fields)-2], " "),
			Kind:       fields[len(fields)-2],
			Confidence: confidence,
		}, nil
	}
	return ProfileSuggestion{}, nil
}

// LabelScene speichert die Bewegungssignatur des Videos unter label (siehe
// --label-scene, motion_signature.py) - die Grundlage, gegen die
// SuggestProfile spätere, ähnliche Szenen misst.
func LabelScene(videoPath, label string) error {
	return LabelSceneWithProfile(videoPath, label, "")
}

// LabelSceneWithProfile stores the measured scene signature together with the
// generator profile explicitly selected by the user. Older callers can keep
// using LabelScene; profile-aware records are the training data for the local
// Go motion-profile model.
func LabelSceneWithProfile(videoPath, label, profile string) error {
	py, err := FindPython()
	if err != nil {
		return err
	}
	if err := CheckDependencies(); err != nil {
		return err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return err
	}
	defer cleanupScriptTemp(scriptPath)
	args := []string{scriptPath, "--video", videoPath, "--label-scene", label}
	if profile != "" {
		args = append(args, "--scene-profile", profile)
	}
	out, err := command(py, args...).CombinedOutput()
	if err != nil {
		return fmt.Errorf("generator: scene could not be saved: %w\n%s", err, string(out))
	}
	return nil
}

func splitLines(s string) []string {
	var lines []string
	start := 0
	for i, r := range s {
		if r == '\n' {
			lines = append(lines, s[start:i])
			start = i + 1
		}
	}
	if start < len(s) {
		lines = append(lines, s[start:])
	}
	return lines
}

func Generate(videoPath string, roi ROI, outputPath string, opts Options, onProgress func(line string)) error {
	return GenerateWithProgress(videoPath, roi, outputPath, opts, onProgress, nil)
}

func GenerateWithProgress(videoPath string, roi ROI, outputPath string, opts Options, onProgress func(line string), onPercent func(pct int)) error {
	return GenerateWithContext(context.Background(), videoPath, roi, outputPath, opts, onProgress, onPercent)
}

// GenerateWithContext runs generation under ctx. Cancel ctx to kill the
// Python subprocess (review: generation must be abortable) or abort native
// tracking mid-loop. Returns context.Canceled when aborted.
//
// Product path (one strong tracker — no “weak fallback” story, #120):
//  1. Go CSRT (trackcv) when OpenCV is linked in this binary (Windows
//     portable ships MinGW OpenCV DLLs — docs/WINDOWS_OPENCV.md)
//  2. Else Python CSRT (opencv-contrib) on builds without linked OpenCV,
//     or when options force Python (PerSceneROI / AI opinion / soft masks)
//
// PreferPython forces the Python path. PreferSimpletrack opts into the
// experimental NCC tracker (lab/CLI); the GUI never sets it.
// Eligible Go-CSRT failures are NOT soft-failed to Python (hides Go bugs).
func GenerateWithContext(ctx context.Context, videoPath string, roi ROI, outputPath string, opts Options, onProgress func(line string), onPercent func(pct int)) error {
	if ctx == nil {
		ctx = context.Background()
	}
	logging.Info("generator: starting generation", "video", videoPath, "roi", fmt.Sprintf("%+v", roi), "output", outputPath)
	phaseT0 := time.Now()

	opts = applyStrokePreview(ctx, videoPath, opts, onProgress)
	logging.Info("generator: phase", "name", "stroke_preview", "ms", time.Since(phaseT0).Milliseconds())
	opts = applyDefaultDetrend(opts, onProgress)
	// Rel35 / #338: when this binary links Go CSRT, soft-drop PerSceneROI so
	// Everyday never falls through to pip OpenCV (MIL) for a Python-only toggle.
	opts = softenPythonOnlyOptsForGoCSRT(opts, roi, onProgress)

	if !opts.PreferPython && NativePipelineEligible(opts, roi) {
		if NativeTrackingAvailable() {
			err := GenerateNativeCSRT(ctx, videoPath, roi, outputPath, opts, onProgress, onPercent)
			if err == nil {
				logging.Info("generator: native CSRT generation finished", "output", outputPath)
				return nil
			}
			return err
		}
		if opts.PreferSimpletrack {
			if onProgress != nil {
				onProgress("Go simpletrack (NCC) — experimental PreferSimpletrack")
			}
			err := GenerateNativeSimple(ctx, videoPath, roi, outputPath, opts, onProgress, onPercent)
			if err == nil {
				logging.Info("generator: native simpletrack generation finished", "output", outputPath)
				return nil
			}
			return err
		}
		// No linked OpenCV in this binary: Python CSRT is the product path.
		// Missing opencv-contrib is a hard error — install it rather than
		// silently degrading to weak NCC.
		if onProgress != nil {
			onProgress("Generate: Python CSRT (this binary has no linked OpenCV)")
		}
	} else if onProgress != nil {
		// Make path choice visible (#338): Owner saw “CSRT not available in
		// OpenCV 5.0.0” and thought the portable lacked CSRT — often the
		// Go CSRT binary was fine and PerSceneROI forced Python instead.
		if opts.PreferPython {
			onProgress("Generate: PreferPython — using Python path (needs opencv-contrib-python with CSRT)")
		} else if NativeTrackingAvailable() {
			if reason := NativePipelineSkipReason(opts, roi); reason != "" {
				onProgress("Generate: Go CSRT skipped — " + reason + " — using Python (needs opencv-contrib-python with CSRT; or uncheck Re-find region for built-in Go CSRT)")
			}
		}
	}

	py, err := FindPython()
	if err != nil {
		return fmt.Errorf("generator: Generate needs Python with opencv-contrib-python (or a build with linked OpenCV CSRT): %w", err)
	}
	if err := CheckDependencies(); err != nil {
		return fmt.Errorf("generator: Generate needs opencv-contrib-python (CSRT). Install with the pip line below — NCC is not the product path.\n%w", err)
	}
	if ok, detail := pythonHasCSRT(py); !ok {
		return fmt.Errorf("generator: Python OpenCV has no CSRT (%s) — needs opencv-contrib-python (no KCF/MIL fallback). Fix: pip uninstall opencv-python opencv-python-headless && pip install opencv-contrib-python", detail)
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return err
	}
	defer cleanupScriptTemp(scriptPath)
	args := buildArgs(scriptPath, videoPath, outputPath, roi, opts)
	cmd := commandContext(ctx, py, args...)
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("generator: start failed: %w", err)
	}
	scanner := bufio.NewScanner(stderr)
	var lastLines []string
	for scanner.Scan() {
		if err := ctx.Err(); err != nil {
			_ = cmd.Wait()
			return err
		}
		line := scanner.Text()
		if done, total, ok := parseProgress(line); ok {
			if onPercent != nil {
				onPercent(percentOf(done, total))
			}
			continue
		}
		lastLines = append(lastLines, line)
		if len(lastLines) > 20 {
			lastLines = lastLines[1:]
		}
		logging.Debug("generator: " + line)
		if onProgress != nil {
			onProgress(line)
		}
	}
	if err := cmd.Wait(); err != nil {
		if errors.Is(ctx.Err(), context.Canceled) || errors.Is(err, context.Canceled) {
			logging.Info("generator: generation cancelled")
			return context.Canceled
		}
		if errors.Is(ctx.Err(), context.DeadlineExceeded) || errors.Is(err, context.DeadlineExceeded) {
			return context.DeadlineExceeded
		}
		logging.Error("generator: generation failed", "error", err)
		return fmt.Errorf("generator: generation failed: %w\nLast output:\n%s", err, joinLines(lastLines))
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	logging.Info("generator: generation finished", "output", outputPath)
	return nil
}

func buildArgs(scriptPath, videoPath, outputPath string, roi ROI, opts Options) []string {
	args := []string{
		scriptPath,
		"--video", videoPath,
		"--output", outputPath,
		"--roi", fmt.Sprintf("%d,%d,%d,%d", roi.X, roi.Y, roi.W, roi.H),
	}
	if opts.Invert {
		args = append(args, "--invert")
	}
	if opts.SmoothWindow > 0 {
		args = append(args, "--smooth-window", strconv.Itoa(opts.SmoothWindow))
	}
	if opts.MinPeakDistanceMs > 0 {
		args = append(args, "--min-peak-distance-ms", strconv.Itoa(opts.MinPeakDistanceMs))
	}
	if opts.MaxFrames > 0 {
		args = append(args, "--max-frames", strconv.Itoa(opts.MaxFrames))
	}
	if opts.DisableCameraCompensation {
		args = append(args, "--no-camera-compensation")
	}
	if opts.RDPTolerance > 0 {
		args = append(args, "--rdp-tolerance", strconv.FormatFloat(opts.RDPTolerance, 'f', -1, 64))
	}
	if opts.DisableSceneCutDetection {
		args = append(args, "--no-scene-cut-detection")
	}
	// OpenCL: Python enables automatically when available (log only).
	// Do not force a separate path — Go CSRT ignores OpenCL; use --no-opencl
	// on the Python CLI to skip auto-enable.
	if opts.Threads > 0 {
		args = append(args, "--threads", strconv.Itoa(opts.Threads))
	}
	if opts.ReportPath != "" {
		args = append(args, "--report", opts.ReportPath)
	}
	if opts.Backend != "" && opts.Backend != "csrt" {
		args = append(args, "--backend", opts.Backend)
	}
	if opts.Profile == "weich" || opts.Profile == "tf" || opts.Profile == "tj" || opts.Profile == "autotune" {
		args = append(args, "--profile", opts.Profile)
	}
	if opts.ROI2.W > 0 && opts.ROI2.H > 0 {
		args = append(args, "--roi2", fmt.Sprintf("%d,%d,%d,%d", opts.ROI2.X, opts.ROI2.Y, opts.ROI2.W, opts.ROI2.H))
		if opts.ROI2Fixed {
			args = append(args, "--roi2-fixed")
		}
	}
	if opts.RegionClass != "" {
		args = append(args, "--region-class", opts.RegionClass)
	}
	if opts.RegionClass2 != "" {
		args = append(args, "--region-class2", opts.RegionClass2)
	}
	for _, t := range opts.ExtraTargets {
		if t.W <= 0 || t.H <= 0 {
			continue
		}
		spec := fmt.Sprintf("%d,%d,%d,%d", t.X, t.Y, t.W, t.H)
		if t.Class != "" {
			spec += "," + t.Class
		}
		args = append(args, "--target", spec)
	}
	for _, m := range opts.MaskROIs {
		if m.W <= 0 || m.H <= 0 {
			continue
		}
		args = append(args, "--mask", fmt.Sprintf("%d,%d,%d,%d", m.X, m.Y, m.W, m.H))
	}
	if opts.PeakProminence > 0 {
		args = append(args, "--peak-prominence", strconv.FormatFloat(opts.PeakProminence, 'f', -1, 64))
	}
	if opts.DynamicRangeMs > 0 {
		args = append(args, "--dynamic-range-ms", strconv.FormatFloat(opts.DynamicRangeMs, 'f', -1, 64))
	}
	if opts.MinActionIntervalMs > 0 {
		args = append(args, "--min-action-interval-ms", strconv.FormatFloat(opts.MinActionIntervalMs, 'f', -1, 64))
	} else if opts.MinActionIntervalMs < 0 {
		args = append(args, "--min-action-interval-ms", "0")
	}
	if opts.MaxSpeed > 0 {
		args = append(args, "--max-speed", strconv.FormatFloat(opts.MaxSpeed, 'f', -1, 64))
	}
	if opts.DetrendWindowMs > 0 {
		args = append(args, "--detrend-ms", strconv.FormatFloat(opts.DetrendWindowMs, 'f', -1, 64))
	}
	if opts.BandpassLowHz > 0 || opts.BandpassHighHz > 0 {
		lo, hi := opts.BandpassLowHz, opts.BandpassHighHz
		if lo <= 0 {
			lo = 0.5
		}
		if hi <= 0 {
			hi = 4
		}
		args = append(args, "--bandpass-hz",
			strconv.FormatFloat(lo, 'f', -1, 64)+","+strconv.FormatFloat(hi, 'f', -1, 64))
	}
	if opts.FlowDownscale > 0 && opts.FlowDownscale != 1 {
		args = append(args, "--flow-downscale", strconv.FormatFloat(opts.FlowDownscale, 'f', -1, 64))
	}
	if opts.Axis == "x" || opts.Axis == "y" {
		// Leer bleibt "auto" (Pythons Standard seit der automatischen
		// Achsenwahl) - nur eine explizite Erzwingung wird weitergereicht.
		args = append(args, "--axis", opts.Axis)
	}
	if opts.AutoRetry {
		args = append(args, "--auto-retry")
	}
	if opts.AdaptiveKeyframeError > 0 {
		args = append(args, "--adaptive-keyframes", strconv.FormatFloat(opts.AdaptiveKeyframeError, 'f', -1, 64))
	}
	if opts.PerSceneROI {
		args = append(args, "--per-scene-roi")
	}
	if opts.DisableCache {
		args = append(args, "--no-cache")
	} else {
		dir := opts.CacheDir
		if dir == "" {
			dir = DefaultCacheDir()
		}
		if dir != "" {
			args = append(args, "--cache-dir", dir)
		}
	}
	if opts.NormPercentile > 0 {
		args = append(args, "--norm-percentile", strconv.FormatFloat(opts.NormPercentile, 'f', -1, 64))
	} else if opts.NormPercentile < 0 {
		args = append(args, "--norm-percentile", "0")
	}
	if opts.AIQualityOpinion {
		args = append(args, "--ai-quality-opinion")
	}
	if opts.AIBaseURL != "" {
		args = append(args, "--ai-base-url", opts.AIBaseURL)
	}
	if opts.ContactVibration {
		args = append(args, "--contact-vibration")
		if opts.ContactVibrationSpan > 0 {
			args = append(args, "--contact-vibration-span",
				strconv.FormatFloat(opts.ContactVibrationSpan, 'f', -1, 64))
		}
		if opts.ContactVibrationCurve != "" && opts.ContactVibrationCurve != "linear" {
			args = append(args, "--contact-vibration-curve", opts.ContactVibrationCurve)
		}
	}
	if opts.AudioCheck {
		args = append(args, "--audio-check")
	}
	if opts.StartTimeSec > 0 {
		args = append(args, "--start-seconds", strconv.FormatFloat(opts.StartTimeSec, 'f', 3, 64))
	}
	return args
}

func BuildArgsForTest(opts Options) []string {
	return buildArgs("script.py", "v.mp4", "o.funscript", ROI{}, opts)
}

func joinLines(lines []string) string {
	out := ""
	for _, l := range lines {
		out += l + "\n"
	}
	return out
}

// ScriptQualityResult ist die Antwort von --script-quality (siehe
// generate_funscript.py) - derselbe Vertrag wie quality_doctor.evaluate()
// selbst zurückgibt, plus EstimatedFromScriptOnly als Warnhinweis: ohne
// Video fehlen die trackingbasierten Prüfungen (aktiver Zeitanteil,
// Rekonstruktionsfehler, Tracker-Verlust, Bewegungsspielraum), das Ergebnis
// ist darum vorsichtiger zu lesen als nach einer echten Generierung.
//
// Kind is always "signal_quality" (docs/SIGNAL_VS_FIDELITY.md) — not Motion
// Fidelity against video/reference.
type ScriptQualityResult struct {
	Score                   float64  `json:"score"`
	Passed                  bool     `json:"passed"`
	Warnings                []string `json:"warnings"`
	EstimatedFromScriptOnly bool     `json:"estimatedFromScriptOnly"`
	Kind                    string   `json:"kind"`
}

// ScriptQuality wendet Quality Doctor auf eine bereits vorhandene
// .funscript-Datei an, ohne Video - z.B. eine aus einem anderen Werkzeug
// importierte Datei ("Script Doctor"). Pure Go (funscript.EvaluateScriptQuality);
// no Python install required for this check.
//
// Actions are read in file order (not via Load/Parse, which sorts) so that
// unsorted timestamps are still flagged — same as quality_doctor on the
// Python --script-quality path.
func ScriptQuality(funscriptPath string) (ScriptQualityResult, error) {
	data, err := os.ReadFile(funscriptPath)
	if err != nil {
		return ScriptQualityResult{}, fmt.Errorf("generator: script could not be read: %w", err)
	}
	var doc struct {
		Actions []funscript.Action `json:"actions"`
		General []funscript.Action `json:"general"` // .samn native
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		return ScriptQualityResult{}, fmt.Errorf("generator: invalid JSON: %w", err)
	}
	actions := doc.Actions
	if len(actions) == 0 {
		actions = doc.General
	}
	if len(actions) == 0 {
		return ScriptQualityResult{}, fmt.Errorf("generator: no actions found in script")
	}
	got := funscript.EvaluateScriptQuality(actions)
	return ScriptQualityResult{
		Score:                   got.Score,
		Passed:                  got.Passed,
		Warnings:                got.Warnings,
		EstimatedFromScriptOnly: true,
		Kind:                    "signal_quality",
	}, nil
}

func AddFeedback(reportPath, outputPath, verdict, comment string) error {
	py, err := FindPython()
	if err != nil {
		return err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return err
	}
	defer cleanupScriptTemp(scriptPath)
	args := []string{scriptPath, "--report", reportPath, "--output", outputPath, "--feedback", verdict}
	if comment != "" {
		args = append(args, comment)
	}
	out, err := command(py, args...).CombinedOutput()
	if err != nil {
		return fmt.Errorf("generator: verdict could not be saved: %w\n%s", err, string(out))
	}
	return nil
}

func ReportSummary(reportPath string) (string, error) {
	py, err := FindPython()
	if err != nil {
		return "", err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return "", err
	}
	defer cleanupScriptTemp(scriptPath)
	out, err := command(py, scriptPath, "--report", reportPath, "--report-summary").Output()
	if err != nil {
		return "", fmt.Errorf("generator: report could not be evaluated: %w", err)
	}
	return string(out), nil
}

func HardwareInfo() (string, error) {
	py, err := FindPython()
	if err != nil {
		return "", err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return "", err
	}
	defer cleanupScriptTemp(scriptPath)
	out, err := command(py, scriptPath, "--hardware-info").Output()
	if err != nil {
		return "", fmt.Errorf("generator: hardware query failed: %w", err)
	}
	return string(out), nil
}

func TrainQualityModel(reportPath string) (string, error) {
	py, err := FindPython()
	if err != nil {
		return "", err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return "", err
	}
	defer cleanupScriptTemp(scriptPath)
	cmd := command(py, scriptPath, "--report", reportPath, "--train-model")
	out, err := cmd.CombinedOutput()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok && exitErr.ExitCode() == 2 {
			return string(out), nil
		}
		return string(out), fmt.Errorf("generator: training failed: %w", err)
	}
	return string(out), nil
}

func QualityModelInfo() (string, error) {
	py, err := FindPython()
	if err != nil {
		return "", err
	}
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		return "", err
	}
	defer cleanupScriptTemp(scriptPath)
	out, err := command(py, scriptPath, "--model-info").Output()
	if err != nil {
		return "", fmt.Errorf("generator: model query failed: %w", err)
	}
	return string(out), nil
}

// BenchmarkCorrelation ist das Ergebnis von fungen_compare.best_lag_correlation()
// für einen Golden-Clip-Eintrag mit hinterlegter FunGen-Referenz - nur
// vorhanden, wenn der Manifest-Eintrag ein reference_funscript trägt.
type BenchmarkCorrelation struct {
	R             float64  `json:"r"`
	LagMs         int      `json:"lag_ms"`
	Orientation   string   `json:"orientation"`
	NSamples      int      `json:"n_samples"`
	ShapeError    *float64 `json:"shape_error"`
	LowConfidence bool     `json:"low_confidence"`
}

// BenchmarkClipResult ist ein einzelner Clip aus golden_clip_benchmark.py's
// run_clip(). OK=false bedeutet: dieser Clip ist fehlgeschlagen (siehe
// Error), hat den restlichen Lauf aber nicht abgebrochen.
type BenchmarkClipResult struct {
	Name            string                `json:"name"`
	OK              bool                  `json:"ok"`
	Error           string                `json:"error,omitempty"`
	QualityScore    *float64              `json:"quality_score"`
	QualityPassed   *bool                 `json:"quality_passed"`
	QualityWarnings []string              `json:"quality_warnings"`
	Correlation     *BenchmarkCorrelation `json:"correlation"`
}

// BenchmarkSummary fasst einen ganzen Lauf zusammen - siehe
// golden_clip_benchmark.summarize().
type BenchmarkSummary struct {
	Total              int      `json:"total"`
	OK                 int      `json:"ok"`
	Failed             int      `json:"failed"`
	QualityPassed      int      `json:"quality_passed"`
	MeanQualityScore   *float64 `json:"mean_quality_score"`
	MeanCorrelation    *float64 `json:"mean_correlation"`
	ClipsWithReference int      `json:"clips_with_reference"`
}

// BenchmarkResult ist ein vollständiger Golden-Clip-Benchmark-Lauf - jede
// Zeile der Verlaufsdatei (--history) ist genau eine BenchmarkResult als
// JSON, siehe golden_clip_benchmark.run_benchmark()/append_history().
type BenchmarkResult struct {
	Timestamp string                `json:"timestamp"`
	GitCommit string                `json:"git_commit"`
	Manifest  string                `json:"manifest"`
	Clips     []BenchmarkClipResult `json:"clips"`
	Summary   BenchmarkSummary      `json:"summary"`
}

// RunGoldenClipBenchmark führt generator/golden_clip_benchmark.py gegen ein
// Manifest fester Vergleichs-Clips aus (docs/NEXT.md Priorität 2: "Reproduce
// before changing the algorithm" - eine feste, wiederholbare Vergleichsbasis
// statt Einzelmessungen). historyPath="" bedeutet: kein Verlaufseintrag,
// nur der aktuelle Lauf.
func RunGoldenClipBenchmark(manifestPath, historyPath string, onProgress func(line string), onPercent func(pct int)) (BenchmarkResult, error) {
	return RunGoldenClipBenchmarkCtx(context.Background(), manifestPath, historyPath, onProgress, onPercent)
}

// RunGoldenClipBenchmarkCtx is RunGoldenClipBenchmark bound to ctx:
// cancelling stops the benchmark Python (GUI cancel, app close). A clip
// generate it already started finishes that one clip on its own.
func RunGoldenClipBenchmarkCtx(ctx context.Context, manifestPath, historyPath string, onProgress func(line string), onPercent func(pct int)) (BenchmarkResult, error) {
	var result BenchmarkResult
	py, err := FindPython()
	if err != nil {
		return result, err
	}
	if err := CheckDependencies(); err != nil {
		return result, err
	}
	mainScript, err := writeScriptToTemp()
	if err != nil {
		return result, err
	}
	defer cleanupScriptTemp(mainScript)
	scriptPath := filepath.Join(filepath.Dir(mainScript), "golden_clip_benchmark.py")

	jsonOut, err := os.CreateTemp("", "golden-clip-result-*.json")
	if err != nil {
		return result, fmt.Errorf("generator: temp file for benchmark result: %w", err)
	}
	jsonOutPath := jsonOut.Name()
	jsonOut.Close()
	defer os.Remove(jsonOutPath)

	args := []string{scriptPath, "--manifest", manifestPath, "--json-output", jsonOutPath}
	if historyPath != "" {
		args = append(args, "--history", historyPath)
	}
	cmd := commandContext(ctx, py, args...)
	cmd.WaitDelay = 5 * time.Second
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return result, fmt.Errorf("generator: stderr-Pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return result, fmt.Errorf("generator: start failed: %w", err)
	}
	scanner := bufio.NewScanner(stderr)
	var lastLines []string
	for scanner.Scan() {
		line := scanner.Text()
		if done, total, ok := parseProgress(line); ok {
			if onPercent != nil {
				onPercent(percentOf(done, total))
			}
			continue
		}
		lastLines = append(lastLines, line)
		if len(lastLines) > 20 {
			lastLines = lastLines[1:]
		}
		logging.Debug("generator: " + line)
		if onProgress != nil {
			onProgress(line)
		}
	}
	if err := cmd.Wait(); err != nil {
		if ctxErr := ctx.Err(); ctxErr != nil {
			return result, fmt.Errorf("generator: golden-clip benchmark cancelled: %w", ctxErr)
		}
		return result, fmt.Errorf("generator: golden-clip benchmark failed: %w\nLast output:\n%s", err, joinLines(lastLines))
	}
	data, err := os.ReadFile(jsonOutPath)
	if err != nil {
		return result, fmt.Errorf("generator: benchmark result not readable: %w", err)
	}
	if err := json.Unmarshal(data, &result); err != nil {
		return result, fmt.Errorf("generator: benchmark result invalid: %w", err)
	}
	return result, nil
}
