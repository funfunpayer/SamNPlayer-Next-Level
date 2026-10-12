//go:build suite

package main

import (
	"sync"
	"testing"
)

// Der Golden-Clip-Benchmark läuft Minuten. Vorher: kein Abbruch, beim
// Schließen der App lief das Python weiter, und ein zweiter Start (Doppel-
// klick) lief parallel und schrieb in dieselbe History-Datei.
func TestBenchmarkRunClaimCancel(t *testing.T) {
	a := &App{}
	if a.CancelGoldenClipBenchmark() {
		t.Fatal("nothing running yet")
	}
	ctx, err := a.claimBenchmarkRun()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := a.claimBenchmarkRun(); err == nil {
		t.Fatal("second run must be refused while one is active")
	}
	if !a.CancelGoldenClipBenchmark() {
		t.Fatal("cancel must report the running benchmark")
	}
	select {
	case <-ctx.Done():
	default:
		t.Fatal("run context not cancelled")
	}
	a.releaseBenchmarkRun()
	if a.CancelGoldenClipBenchmark() {
		t.Fatal("released run must not count as running")
	}
}

func TestBenchmarkRunClaimIsExclusive(t *testing.T) {
	a := &App{}
	var wg sync.WaitGroup
	var mu sync.Mutex
	won := 0
	for i := 0; i < 32; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := a.claimBenchmarkRun(); err == nil {
				mu.Lock()
				won++
				mu.Unlock()
			}
		}()
	}
	wg.Wait()
	a.releaseBenchmarkRun()
	if won != 1 {
		t.Fatalf("%d concurrent claims succeeded, want exactly 1", won)
	}
}
