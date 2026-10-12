package main

import (
	"context"
	"errors"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator"
)

func TestRunGeneratePropagatesCanceledContext(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	called := false
	generate := func(got context.Context, _ string, _ generator.ROI, _ string, _ generator.Options, _ func(string), _ func(int)) error {
		called = true
		if !errors.Is(got.Err(), context.Canceled) {
			t.Fatalf("generator context error = %v, want context.Canceled", got.Err())
		}
		return got.Err()
	}

	code := runGenerateWithContext(ctx, []string{
		"--video", "clip.mp4",
		"--roi", "10,20,30,40",
		"--output", "out.funscript",
	}, generate)
	if !called {
		t.Fatal("generator was not called")
	}
	if code != 1 {
		t.Fatalf("exit code = %d, want 1", code)
	}
}
