package generator

import (
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestParseAISetupReport(t *testing.T) {
	r, err := parseAISetupReport([]byte(`{"gpus":[{"name":"RTX 4060 Ti","vram_gb":16.0,"driver":"560"}],
		"packages":{"nudenet":false,"onnxruntime":true},"system":{"os":"Windows"},
		"train_venv":{"exists":false},
		"servers":{"ollama":{"url":"http://127.0.0.1:11434","up":true,"models":["qwen2.5vl:7b"]}},
		"next_steps":["teachers: install"]}`))
	if err != nil {
		t.Fatal(err)
	}
	if r.GPUs[0].VRAMGB != 16 || !r.Packages["onnxruntime"] || !r.Servers["ollama"].Up ||
		r.Servers["ollama"].Models[0] != "qwen2.5vl:7b" || len(r.NextSteps) != 1 {
		t.Fatalf("parsed wrong: %+v", r)
	}
	if _, err := parseAISetupReport([]byte("nope")); err == nil {
		t.Fatal("want error")
	}
}

func TestInstallAISetupRejectsUnknownProfile(t *testing.T) {
	if err := InstallAISetup("everything-please", nil); err == nil {
		t.Fatal("want error")
	}
}

func TestContactPointsArgs(t *testing.T) {
	if _, _, err := contactPointsArgs("v.mp4", ContactPointsRun{}); err == nil {
		t.Fatal("no teacher must be an error")
	}
	args, out, err := contactPointsArgs(filepath.Join("clips", "v.mp4"), ContactPointsRun{
		NudeNet: true, Teachers: []string{"ollama:qwen2.5vl:7b", "colibri:glm-5.3-flash"},
		Onnx: []string{"m.onnx"}, StepS: 0.25})
	if err != nil {
		t.Fatal(err)
	}
	if out != filepath.Join("clips", "v.contact.json") {
		t.Fatalf("out = %s", out)
	}
	got := strings.Join(args, " ")
	want := "--video " + filepath.Join("clips", "v.mp4") + " --out " + out +
		" --nudenet --teacher ollama:qwen2.5vl:7b --teacher colibri:glm-5.3-flash --onnx m.onnx --step-s 0.25"
	if got != want {
		t.Fatalf("args:\n got %s\nwant %s", got, want)
	}
}

// The embedded scripts really run through the same path the GUI buttons
// use (skipped where no Python is installed).
func TestRunEmbeddedPythonAISetupDryRun(t *testing.T) {
	if _, err := exec.LookPath("python3"); err != nil {
		t.Skip("no python3")
	}
	var lines []string
	out, err := runEmbeddedPython("ai_setup.py", []string{"install", "teachers"}, func(l string) { lines = append(lines, l) })
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out, "--no-deps nudenet") || !strings.Contains(out, "dry run") {
		t.Fatalf("unexpected dry-run output:\n%s", out)
	}
	if len(lines) == 0 {
		t.Fatal("no streamed lines")
	}
}
