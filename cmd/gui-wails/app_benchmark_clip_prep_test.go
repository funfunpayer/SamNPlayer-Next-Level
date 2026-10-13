//go:build suite

package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/videox"
)

func TestSuggestBenchmarkClipOutput(t *testing.T) {
	a := NewApp()
	got, err := a.SuggestBenchmarkClipOutput("/vids/long.mp4", "01:20", "02:05")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/vids", "long_01-20-02-05.mp4")
	// On Windows Join may differ; compare base + dir parts via Clean.
	if filepath.Clean(got) != filepath.Clean(want) && !strings.HasSuffix(got, "long_01-20-02-05.mp4") {
		t.Fatalf("got %q want suffix long_01-20-02-05.mp4", got)
	}
}

func TestResolveClipWindowFromStrings(t *testing.T) {
	start, end, err := resolveClipWindow(BenchmarkClipExportRequest{
		Start: "1:20",
		End:   "2:05",
	})
	if err != nil {
		t.Fatal(err)
	}
	if start != 80 || end != 125 {
		t.Fatalf("got %v–%v", start, end)
	}
	_, _, err = resolveClipWindow(BenchmarkClipExportRequest{StartSec: 10, EndSec: 5})
	if err == nil {
		t.Fatal("expected end-before-start error")
	}
}

func TestExportBenchmarkClipRejectsMissingSource(t *testing.T) {
	a := NewApp()
	_, err := a.ExportBenchmarkClip(BenchmarkClipExportRequest{
		Source: "",
		Output: filepath.Join(t.TempDir(), "out.mp4"),
		Start:  "0",
		End:    "1",
	})
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestExportBenchmarkClipRunsFFmpeg(t *testing.T) {
	if _, err := exec.LookPath("ffmpeg"); err != nil {
		t.Skip("ffmpeg not available")
	}
	videox.ResetToolCache()
	dir := t.TempDir()
	src := filepath.Join(dir, "src.mp4")
	args := []string{
		"-v", "error", "-y",
		"-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=3",
		"-pix_fmt", "yuv420p", src,
	}
	if out, err := exec.Command("ffmpeg", args...).CombinedOutput(); err != nil {
		t.Fatalf("make source: %v: %s", err, out)
	}
	out := filepath.Join(dir, "clip.mp4")
	a := NewApp()
	res, err := a.ExportBenchmarkClip(BenchmarkClipExportRequest{
		Source:    src,
		Output:    out,
		Start:     "0.5",
		End:       "2.0",
		PresetRes: "720p",
		NoAudio:   true,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.MaxWidth != 1280 {
		t.Fatalf("maxWidth: %d", res.MaxWidth)
	}
	if res.DurationSec < 1.4 || res.DurationSec > 1.6 {
		t.Fatalf("duration: %v", res.DurationSec)
	}
	st, err := os.Stat(out)
	if err != nil || st.Size() < 100 {
		t.Fatalf("output missing/small: %v", err)
	}
}

func TestParseBenchmarkClipTime(t *testing.T) {
	a := NewApp()
	sec, err := a.ParseBenchmarkClipTime("00:01:30")
	if err != nil || sec != 90 {
		t.Fatalf("got %v %v", sec, err)
	}
}
