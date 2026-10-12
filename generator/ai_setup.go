package generator

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"path/filepath"
	"strconv"
	"strings"
)

// AISetupReport is `ai_setup.py check --json`: what this PC has for the
// local AI paths (GPU, packages, training venv, local model servers) and
// the next steps in plain words. For a Settings "Check AI setup" button.
type AISetupReport struct {
	GPUs []struct {
		Name   string  `json:"name"`
		VRAMGB float64 `json:"vram_gb"`
		Driver string  `json:"driver"`
	} `json:"gpus"`
	Packages  map[string]bool `json:"packages"`
	System    map[string]any  `json:"system"`
	TrainVenv map[string]any  `json:"train_venv"`
	Servers   map[string]struct {
		URL    string   `json:"url"`
		Up     bool     `json:"up"`
		Models []string `json:"models"`
	} `json:"servers"`
	NextSteps []string `json:"next_steps"`
}

// AISetupProfiles are the `ai_setup.py install` profiles.
var AISetupProfiles = []string{"teachers", "train", "models", "all"}

func parseAISetupReport(b []byte) (AISetupReport, error) {
	var r AISetupReport
	if err := json.Unmarshal(b, &r); err != nil {
		return r, fmt.Errorf("generator: ai_setup report: %w", err)
	}
	return r, nil
}

// CheckAISetup runs the embedded ai_setup.py check. It changes nothing.
func CheckAISetup() (AISetupReport, error) {
	out, err := runEmbeddedPython("ai_setup.py", []string{"check", "--json"}, nil)
	if err != nil {
		return AISetupReport{}, err
	}
	return parseAISetupReport([]byte(out))
}

// InstallAISetup runs `ai_setup.py install <profile> --yes` and streams its
// lines. "train" installs into a separate venv, so the app's OpenCV (CSRT)
// is never touched; "teachers" installs NudeNet with --no-deps for the same
// reason.
func InstallAISetup(profile string, onLine func(string)) error {
	ok := false
	for _, p := range AISetupProfiles {
		ok = ok || p == profile
	}
	if !ok {
		return fmt.Errorf("generator: unknown AI setup profile %q", profile)
	}
	_, err := runEmbeddedPython("ai_setup.py", []string{"install", profile, "--yes"}, onLine)
	return err
}

// ContactPointsRun selects the teachers for GenerateContactPoints.
type ContactPointsRun struct {
	NudeNet  bool
	Teachers []string // "backend:model", e.g. ollama:qwen2.5vl:7b, colibri:glm-5.3-flash
	Onnx     []string // own RF-DETR models (contact_detector.py)
	StepS    float64  // NudeNet / ONNX sampling step (0 = 0.5 s)
	Out      string   // default: <video>.contact.json
}

func contactPointsArgs(video string, o ContactPointsRun) ([]string, string, error) {
	if !o.NudeNet && len(o.Teachers) == 0 && len(o.Onnx) == 0 {
		return nil, "", fmt.Errorf("generator: no teacher selected (NudeNet, a VLM teacher or an ONNX model)")
	}
	out := o.Out
	if out == "" {
		out = strings.TrimSuffix(video, filepath.Ext(video)) + ".contact.json"
	}
	args := []string{"--video", video, "--out", out}
	if o.NudeNet {
		args = append(args, "--nudenet")
	}
	for _, t := range o.Teachers {
		args = append(args, "--teacher", t)
	}
	for _, m := range o.Onnx {
		args = append(args, "--onnx", m)
	}
	if o.StepS > 0 {
		args = append(args, "--step-s", strconv.FormatFloat(o.StepS, 'f', -1, 64))
	}
	return args, out, nil
}

// GenerateContactPoints runs the embedded contact_points.py with the chosen
// teachers and returns the written <video>.contact.json - the file the
// Advanced "Use contact points" option loads. One click instead of the CLI.
func GenerateContactPoints(video string, o ContactPointsRun, onLine func(string)) (string, error) {
	return GenerateContactPointsWithContext(context.Background(), video, o, onLine)
}

// GenerateContactPointsWithContext is GenerateContactPoints with cancel via ctx.
func GenerateContactPointsWithContext(ctx context.Context, video string, o ContactPointsRun, onLine func(string)) (string, error) {
	args, out, err := contactPointsArgs(video, o)
	if err != nil {
		return "", err
	}
	if _, err := runEmbeddedPythonCtx(ctx, "contact_points.py", args, onLine); err != nil {
		return "", err
	}
	return out, nil
}

// runEmbeddedPython extracts the embedded generator scripts, runs one of
// them with the detected Python, streams combined output lines to onLine
// and returns stdout.
func runEmbeddedPython(script string, args []string, onLine func(string)) (string, error) {
	return runEmbeddedPythonCtx(context.Background(), script, args, onLine)
}

func runEmbeddedPythonCtx(ctx context.Context, script string, args []string, onLine func(string)) (string, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	py, err := FindPython()
	if err != nil {
		return "", err
	}
	main, err := writeScriptToTemp()
	if err != nil {
		return "", err
	}
	defer cleanupScriptTemp(main)
	cmd := commandContext(ctx, py, append([]string{filepath.Join(filepath.Dir(main), script)}, args...)...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return "", err
	}
	var errBuf strings.Builder
	cmd.Stderr = io.MultiWriter(&errBuf, lineWriter(onLine))
	if err := cmd.Start(); err != nil {
		return "", err
	}
	var outBuf strings.Builder
	sc := bufio.NewScanner(stdout)
	sc.Buffer(make([]byte, 1<<20), 1<<24)
	for sc.Scan() {
		line := sc.Text()
		outBuf.WriteString(line + "\n")
		if onLine != nil {
			onLine(line)
		}
	}
	if err := cmd.Wait(); err != nil {
		if ctx.Err() != nil {
			return outBuf.String(), fmt.Errorf("generator: %s cancelled: %w", script, ctx.Err())
		}
		msg := strings.TrimSpace(errBuf.String())
		if i := strings.LastIndex(msg, "\n"); i >= 0 {
			msg = msg[i+1:]
		}
		return outBuf.String(), fmt.Errorf("generator: %s failed: %w (%s)", script, err, msg)
	}
	return outBuf.String(), nil
}

type lineWriterFunc func(string)

func (f lineWriterFunc) Write(p []byte) (int, error) {
	for _, l := range strings.Split(strings.TrimRight(string(p), "\n"), "\n") {
		if l != "" {
			f(l)
		}
	}
	return len(p), nil
}

func lineWriter(onLine func(string)) io.Writer {
	if onLine == nil {
		return io.Discard
	}
	return lineWriterFunc(onLine)
}
