package generator

import (
	"context"
	"errors"
	"os/exec"
	"runtime"
	"testing"
	"time"
)

// Das YOLO-Training kann Stunden laufen. Vorher startete es Python ohne
// Kontext: kein Abbruch möglich, und beim Schließen der App lief der
// Prozess unsichtbar weiter. Mit Kontext endet er beim Abbruch.
func TestRunPythonScriptCtxStopsOnCancel(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("uses sleep(1)")
	}
	sleep, err := exec.LookPath("sleep")
	if err != nil {
		t.Skip("no sleep binary")
	}
	ctx, cancel := context.WithCancel(context.Background())
	time.AfterFunc(100*time.Millisecond, cancel)
	start := time.Now()
	err = runPythonScriptCtx(ctx, sleep, []string{"30"}, "test", nil, nil)
	if took := time.Since(start); took > 10*time.Second {
		t.Fatalf("process not stopped on cancel (took %s)", took)
	}
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("want context.Canceled, got %v", err)
	}
}
