//go:build suite

package main

import (
	"sync"
	"testing"
)

// Ein laufender Bootstrap/Trainingslauf lässt sich abbrechen, und beim
// Schließen der App wird er abgebrochen (shutdown ruft CancelRoiTraining).
func TestRoiTrainingRunCancel(t *testing.T) {
	a := &App{}
	if a.CancelRoiTraining() {
		t.Fatal("nothing running yet")
	}
	ctx, err := claimRoiTrainingRun()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := claimRoiTrainingRun(); err == nil {
		t.Fatal("second claim must fail while a run is active")
	}
	if !a.CancelRoiTraining() {
		t.Fatal("cancel must report the running job")
	}
	select {
	case <-ctx.Done():
	default:
		t.Fatal("run context not cancelled")
	}
	releaseRoiTrainingRun()
	if a.CancelRoiTraining() {
		t.Fatal("released run must not count as running")
	}
}

// claim/release liefen vorher auf einer ungeschützten globalen bool -
// gleichzeitige Aufrufe (Wails-Bindung + Worker-Goroutine) waren ein Race.
func TestRoiTrainingRunClaimIsExclusive(t *testing.T) {
	var wg sync.WaitGroup
	var mu sync.Mutex
	won := 0
	for i := 0; i < 32; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := claimRoiTrainingRun(); err == nil {
				mu.Lock()
				won++
				mu.Unlock()
			}
		}()
	}
	wg.Wait()
	releaseRoiTrainingRun()
	if won != 1 {
		t.Fatalf("%d concurrent claims succeeded, want exactly 1", won)
	}
}
