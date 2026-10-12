package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestThumbTimes(t *testing.T) {
	got := thumbTimes(10_000, 4)
	want := []int64{1250, 3750, 6250, 8750}
	if fmt.Sprint(got) != fmt.Sprint(want) {
		t.Fatalf("thumbTimes = %v, want %v", got, want)
	}
	if thumbTimes(0, 4) != nil || thumbTimes(1000, 0) != nil {
		t.Fatal("empty input must give nil")
	}
}

func TestGetVideoThumbnails(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	if _, err := a.GetVideoThumbnails(8); err == nil {
		t.Fatal("no video: want error")
	}
	video := filepath.Join(t.TempDir(), "clip.mp4")
	if err := os.WriteFile(video, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	a.stateMu.Lock()
	a.setVideoPathLocked(video)
	a.stateMu.Unlock()

	oldDur, oldFrame := thumbDurationFn, thumbFrameFn
	t.Cleanup(func() {
		thumbDurationFn, thumbFrameFn = oldDur, oldFrame
		thumbCache.key, thumbCache.thumbs = "", nil
	})
	thumbDurationFn = func(context.Context, string) (time.Duration, error) { return 60 * time.Second, nil }
	var calls int32
	thumbFrameFn = func(_ context.Context, _ string, atMs int64, out string) error {
		atomic.AddInt32(&calls, 1)
		if atMs == 37_500 { // third slot fails
			return fmt.Errorf("boom")
		}
		return os.WriteFile(out, []byte("jpg"), 0o644)
	}
	thumbs, err := a.GetVideoThumbnails(4)
	if err != nil {
		t.Fatal(err)
	}
	if len(thumbs) != 4 || thumbs[0].AtMs != 7_500 || thumbs[3].AtMs != 52_500 {
		t.Fatalf("thumbs = %+v", thumbs)
	}
	if !strings.HasPrefix(thumbs[0].DataURL, "data:image/jpeg;base64,") || thumbs[2].DataURL != "" {
		t.Fatalf("data urls: %q / %q", thumbs[0].DataURL, thumbs[2].DataURL)
	}
	if _, err := a.GetVideoThumbnails(4); err != nil {
		t.Fatal(err)
	}
	if calls != 4 {
		t.Fatalf("second call must hit the cache, ffmpeg calls = %d", calls)
	}
	if got, _ := a.GetVideoThumbnails(500); len(got) != thumbMaxCount {
		t.Fatalf("count clamp: %d", len(got))
	}
}
