//go:build suite

package main

import (
	"strings"
	"testing"
)

func TestLabelSceneWithProfileRejectsUnsupportedProfileBeforeVideoWork(t *testing.T) {
	app := &App{}
	err := app.LabelSceneWithProfile("missing-video.mp4", "scene", "unknown")
	if err == nil || !strings.Contains(err.Error(), "unsupported generator profile") {
		t.Fatalf("expected clear profile validation error, got %v", err)
	}
}
