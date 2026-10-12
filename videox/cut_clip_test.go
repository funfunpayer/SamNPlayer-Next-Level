package videox

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestParseClipPrepTime(t *testing.T) {
	cases := []struct {
		in   string
		want float64
	}{
		{"90", 90},
		{"1:30", 90},
		{"00:01:30", 90},
		{"1:30.5", 90.5},
		{"  2:05  ", 125},
	}
	for _, tc := range cases {
		got, err := ParseClipPrepTime(tc.in)
		if err != nil {
			t.Fatalf("%q: %v", tc.in, err)
		}
		if got != tc.want {
			t.Fatalf("%q: want %v got %v", tc.in, tc.want, got)
		}
	}
	if _, err := ParseClipPrepTime(""); err == nil {
		t.Fatal("empty should fail")
	}
	if _, err := ParseClipPrepTime("a:b:c:d"); err == nil {
		t.Fatal("too many parts should fail")
	}
}

func TestResolveClipPrepMaxWidth(t *testing.T) {
	if got := ResolveClipPrepMaxWidth("720p", 0); got != 1280 {
		t.Fatalf("720p: %d", got)
	}
	if got := ResolveClipPrepMaxWidth("960w", 0); got != 960 {
		t.Fatalf("960w: %d", got)
	}
	if got := ResolveClipPrepMaxWidth("1080p", 0); got != 1920 {
		t.Fatalf("1080p: %d", got)
	}
	if got := ResolveClipPrepMaxWidth("", 640); got != 640 {
		t.Fatalf("override: %d", got)
	}
	if got := ResolveClipPrepMaxWidth("", 0); got != ClipPrepDefaultMaxWidth {
		t.Fatalf("default: %d", got)
	}
}

func TestBuildClipPrepArgsMatchesScriptDefaults(t *testing.T) {
	args, err := BuildClipPrepArgs(ClipPrepOptions{
		Input:    "in.mp4",
		Output:   "out.mp4",
		StartSec: 10,
		EndSec:   55,
		MaxWidth: 1280,
	})
	if err != nil {
		t.Fatal(err)
	}
	joined := strings.Join(args, " ")
	for _, want := range []string{
		"-y", "-ss", "10", "-i", "in.mp4", "-t", "45",
		"scale='min(1280,iw)':-2:flags=lanczos",
		"libx264", "veryfast", "20", "yuv420p", "aac", "128k", "+faststart", "out.mp4",
	} {
		if !strings.Contains(joined, want) {
			t.Fatalf("missing %q in %v", want, args)
		}
	}
}

func TestBuildClipPrepArgsNoAudioAndRejectsBadWindow(t *testing.T) {
	args, err := BuildClipPrepArgs(ClipPrepOptions{
		Input: "in.mp4", Output: "out.mp4", StartSec: 0, EndSec: 5, NoAudio: true,
	})
	if err != nil {
		t.Fatal(err)
	}
	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "-an") {
		t.Fatalf("want -an: %v", args)
	}
	if strings.Contains(joined, "aac") {
		t.Fatalf("no aac when NoAudio: %v", args)
	}
	if _, err := BuildClipPrepArgs(ClipPrepOptions{
		Input: "in.mp4", Output: "out.mp4", StartSec: 10, EndSec: 5,
	}); err == nil {
		t.Fatal("end before start should fail")
	}
}

func TestExportClipPrepCut(t *testing.T) {
	src := makeTestVideo(t) // 2s 320x240
	out := filepath.Join(t.TempDir(), "cut.mp4")
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	err := ExportClipPrepCut(ctx, ClipPrepOptions{
		Input:    src,
		Output:   out,
		StartSec: 0.2,
		EndSec:   1.2,
		MaxWidth: 1280, // source smaller — never upscales
	})
	if err != nil {
		t.Fatal(err)
	}
	st, err := os.Stat(out)
	if err != nil || st.Size() < 100 {
		t.Fatalf("bad output: %v size=%v", err, st)
	}
	info, err := Probe(ctx, out)
	if err != nil {
		t.Fatal(err)
	}
	if info.Width > 320 {
		t.Fatalf("should not upscale: got %dx%d", info.Width, info.Height)
	}
	if info.Duration < 500*time.Millisecond || info.Duration > 1500*time.Millisecond {
		t.Fatalf("unexpected duration %v", info.Duration)
	}
}
