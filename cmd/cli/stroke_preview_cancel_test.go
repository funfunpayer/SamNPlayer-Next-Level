package main

import (
	"context"
	"errors"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator/strokepreview"
)

func TestRunStrokePreviewPropagatesCanceledContext(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	called := false
	analyze := func(got context.Context, _ string, _ strokepreview.Options) (strokepreview.Report, error) {
		called = true
		if !errors.Is(got.Err(), context.Canceled) {
			t.Fatalf("analyzer context error = %v, want context.Canceled", got.Err())
		}
		return strokepreview.Report{}, got.Err()
	}

	code := runStrokePreviewWithContext(ctx, []string{"clip.mp4"}, analyze)
	if !called {
		t.Fatal("analyzer was not called")
	}
	if code != 1 {
		t.Fatalf("exit code = %d, want 1", code)
	}
}
