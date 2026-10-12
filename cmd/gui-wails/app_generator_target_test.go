package main

import (
	"context"
	"testing"
	"time"
)

func requireCanceled(t *testing.T, ctx context.Context) {
	t.Helper()
	select {
	case <-ctx.Done():
		if ctx.Err() != context.Canceled {
			t.Fatalf("context error = %v, want canceled", ctx.Err())
		}
	case <-time.After(time.Second):
		t.Fatal("superseded ROI request was not canceled")
	}
}

func TestBeginROIRequestCancelsSupersededDetectorAndGatesSequence(t *testing.T) {
	a := &App{}
	seq1, ctx1, finish1 := a.beginROIRequest(true)
	seq2, ctx2, finish2 := a.beginROIRequest(true)
	defer finish2()

	if seq2 <= seq1 {
		t.Fatalf("sequence did not advance: first=%d second=%d", seq1, seq2)
	}
	requireCanceled(t, ctx1)
	if a.roiRequestCurrent(seq1) {
		t.Fatal("superseded request still considered current")
	}
	if !a.roiRequestCurrent(seq2) {
		t.Fatal("new request not considered current")
	}

	// Completing the old goroutine must not clear the newer cancel function.
	finish1()
	a.CancelROIDetection()
	requireCanceled(t, ctx2)
	if a.roiRequestCurrent(seq2) {
		t.Fatal("explicit cancellation did not invalidate the current sequence")
	}
}
