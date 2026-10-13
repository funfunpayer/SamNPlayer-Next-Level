package main

import (
	"context"
	"errors"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/player"
)

func TestTrainingDoneMessagesDistinguishesSafetyCeilingFromError(t *testing.T) {
	logMsg, errMsg := trainingDoneMessages(context.DeadlineExceeded)
	if logMsg == "" || errMsg != "" {
		t.Fatalf("safety ceiling must produce a log message, not an error: log=%q err=%q", logMsg, errMsg)
	}
	if !strings.Contains(logMsg, "safety limit") {
		t.Errorf("log message should say what happened: %q", logMsg)
	}

	logMsg, errMsg = trainingDoneMessages(context.Canceled)
	if logMsg != "" || errMsg != "" {
		t.Fatalf("a plain user-requested cancel must stay silent (existing behavior): log=%q err=%q", logMsg, errMsg)
	}

	logMsg, errMsg = trainingDoneMessages(nil)
	if logMsg != "" || errMsg != "" {
		t.Fatalf("no error must stay silent: log=%q err=%q", logMsg, errMsg)
	}

	someErr := errors.New("device write failed")
	logMsg, errMsg = trainingDoneMessages(someErr)
	if errMsg != someErr.Error() || logMsg != "" {
		t.Fatalf("a real failure must still surface as training:error: log=%q err=%q", logMsg, errMsg)
	}
}

// Reporting 9 or 10 must interrupt the RUNNING cycle immediately, not
// only shape the next one - see docs/TRAINING_MODE_RESEARCH.md
// proposal (B). Deliberately behavioral (runs a real training session
// through player.RunTrainingWithControl with the SAME control object
// a.ReportArousal touches) rather than poking at TrainingControl's
// unexported state - App.ReportArousal's effect is what matters here,
// not an internal flag. Deliberately at the App layer (not inside
// player.TrainingControl.ReportArousal itself) per that function's own
// comment.
func TestReportArousalHighValueAlsoStopsCurrentCycle(t *testing.T) {
	a := NewApp()
	control := player.NewTrainingControl()
	a.stateMu.Lock()
	a.trainingControl = control
	a.stateMu.Unlock()

	dev := device.NewMock(false)
	opts := player.TrainingOptions{
		Technique: player.TechniqueStopStart, Channel: player.ChannelVibration,
		Cycles: 1, RampUpMs: 100, HoldMs: 5000, RestMs: 100, PeakIntensity: 1.0,
	}

	var result player.TrainingCycleResult
	var mu sync.Mutex
	done := make(chan error, 1)
	go func() {
		done <- player.RunTrainingWithControl(context.Background(), dev, opts, control,
			func(r player.TrainingCycleResult) {
				mu.Lock()
				result = r
				mu.Unlock()
			})
	}()

	// In die Haltezeit hinein warten, dann per App-Methode hoch melden -
	// wie ein echter Klick auf "9" während eines laufenden Zyklus.
	time.Sleep(300 * time.Millisecond)
	started := time.Now()
	if err := a.ReportArousal(9); err != nil {
		t.Fatalf("ReportArousal(9): %v", err)
	}

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTrainingWithControl: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("training did not stop after a high ReportArousal call")
	}

	if elapsed := time.Since(started); elapsed > 1200*time.Millisecond {
		t.Errorf("stop took %v - ReportArousal(9) should interrupt the running cycle immediately, not wait for the 5s hold", elapsed)
	}
	mu.Lock()
	defer mu.Unlock()
	if !result.StoppedByUser {
		t.Error("the cycle running when ReportArousal(9) was called must be marked StoppedByUser")
	}
}

// A moderate report (below the threshold) must NOT interrupt the running
// cycle - only shape the next one, the existing/unchanged behavior.
func TestReportArousalModerateValueDoesNotStopCurrentCycle(t *testing.T) {
	a := NewApp()
	control := player.NewTrainingControl()
	a.stateMu.Lock()
	a.trainingControl = control
	a.stateMu.Unlock()

	dev := device.NewMock(false)
	opts := player.TrainingOptions{
		Technique: player.TechniqueStopStart, Channel: player.ChannelVibration,
		Cycles: 1, RampUpMs: 100, HoldMs: 600, RestMs: 100, PeakIntensity: 1.0,
	}

	var result player.TrainingCycleResult
	var mu sync.Mutex
	done := make(chan error, 1)
	go func() {
		done <- player.RunTrainingWithControl(context.Background(), dev, opts, control,
			func(r player.TrainingCycleResult) {
				mu.Lock()
				result = r
				mu.Unlock()
			})
	}()

	time.Sleep(200 * time.Millisecond)
	if err := a.ReportArousal(7); err != nil {
		t.Fatalf("ReportArousal(7): %v", err)
	}

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("RunTrainingWithControl: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("training did not finish")
	}

	mu.Lock()
	defer mu.Unlock()
	if result.StoppedByUser {
		t.Error("a moderate report (7) must not interrupt the running cycle")
	}
}

func TestReportArousalNoTrainingRunningIsAnError(t *testing.T) {
	a := NewApp()
	if err := a.ReportArousal(9); err == nil {
		t.Error("reporting with no training running should error, not silently succeed")
	}
}
