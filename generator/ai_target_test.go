package generator

import (
	"context"
	"errors"
	"path/filepath"
	"reflect"
	"testing"
)

func TestExpectedTipAIArgsAreStrictAndDoNotUsePreferences(t *testing.T) {
	model := filepath.Join("models", "roi_detector.onnx")
	got := expectedTipAIArgs(model, "glans", 12.3456)
	want := []string{
		"--expected-class", "glans", "--strict-class",
		"--time-sec", "12.346",
		"--model", model, "--classes-json", filepath.Dir(model),
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("args=%q, want %q", got, want)
	}
	for _, arg := range got {
		if arg == "--preferred-classes" {
			t.Fatal("strict target path must not use the legacy preferred-class fallback")
		}
	}
}

func TestParseTipDetectionLine(t *testing.T) {
	line := `TIP_DETECTION {"x":10,"y":20,"w":30,"h":40,"expectedClass":"glans","matchedClass":"glans","matchedClassId":3,"confidence":0.91,"sampleIndex":25,"match":true,"status":"matched"}`
	got, strictErr, ok := parseTipDetectionLine(line)
	if !ok || strictErr != nil {
		t.Fatalf("parse failed: ok=%v err=%v", ok, strictErr)
	}
	if !got.Match || got.ExpectedClass != "glans" || got.MatchedClass != "glans" || got.Confidence != 0.91 || got.W != 30 {
		t.Fatalf("unexpected detection: %+v", got)
	}
}

func TestParseStrictTipErrorLine(t *testing.T) {
	line := `TIP_ERROR {"code":"target_not_detected","expectedClass":"nipples","message":"No nipples detection","match":false}`
	_, got, ok := parseTipDetectionLine(line)
	if !ok || got == nil || got.Code != "target_not_detected" || got.ExpectedClass != "nipples" {
		t.Fatalf("unexpected strict error: ok=%v err=%+v", ok, got)
	}
	var target *StrictTipDetectionError
	if !errors.As(got, &target) {
		t.Fatalf("typed error is not discoverable with errors.As: %T", got)
	}
}

func TestParseTipDetectionRejectsUnrelatedOutput(t *testing.T) {
	if _, _, ok := parseTipDetectionLine("ROI 1 2 3 4"); ok {
		t.Fatal("legacy coordinate-only output must not become a semantic match")
	}
}

func TestFindExpectedTipROIAIHonorsCanceledContext(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	_, err := FindExpectedTipROIAIWithProgressContext(
		ctx, "unused.mp4", "unused.onnx", "glans", 0, nil, nil)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("error=%v, want context.Canceled", err)
	}
}
