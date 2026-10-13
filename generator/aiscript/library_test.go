package aiscript

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadImitationSamplesSkipsBad(t *testing.T) {
	dir := t.TempDir()
	_, err := ExportImitationSample(dir, ImitationSample{
		Actions: []Action{{At: 0, Pos: 10}, {At: 1000, Pos: 90}},
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "junk.json"), []byte(`{not json`), 0o644); err != nil {
		t.Fatal(err)
	}
	samples, err := LoadImitationSamples(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 1 {
		t.Fatalf("got %d samples", len(samples))
	}
	if CountValidSamples(dir) != 1 {
		t.Fatal("count mismatch")
	}
}

func TestPickBestSamplePrefersDurationAndQD(t *testing.T) {
	pass := true
	fail := false
	samples := []LoadedSample{
		{Sample: ImitationSample{DurationMs: 5000, QDPassed: &fail, Actions: []Action{{At: 0, Pos: 0}, {At: 5000, Pos: 100}}}},
		{Sample: ImitationSample{DurationMs: 9800, QDPassed: &pass, Actions: []Action{{At: 0, Pos: 0}, {At: 9800, Pos: 100}}}},
		{Sample: ImitationSample{DurationMs: 10000, QDPassed: &fail, Actions: []Action{{At: 0, Pos: 0}, {At: 10000, Pos: 50}}}},
	}
	got, err := PickBestSample(samples, 10000, 0, 0)
	if err != nil {
		t.Fatal(err)
	}
	if got.Sample.DurationMs != 9800 {
		t.Fatalf("want QD-passed near 10s, got %d", got.Sample.DurationMs)
	}
}

func TestPickBestSamplePrefersTipAspect(t *testing.T) {
	pass := true
	// Same duration band; wide tip vs tall tip — request is tall (40×80 = 0.5).
	samples := []LoadedSample{
		{Path: "wide.json", Sample: ImitationSample{
			DurationMs: 10000, QDPassed: &pass, TipW: 80, TipH: 40,
			Actions: []Action{{At: 0, Pos: 0}, {At: 10000, Pos: 100}},
		}},
		{Path: "tall.json", Sample: ImitationSample{
			DurationMs: 10100, QDPassed: &pass, TipW: 40, TipH: 80,
			Actions: []Action{{At: 0, Pos: 0}, {At: 10100, Pos: 50}},
		}},
	}
	got, err := PickBestSample(samples, 10000, 40, 80)
	if err != nil {
		t.Fatal(err)
	}
	if got.Path != "tall.json" {
		t.Fatalf("want tall tip aspect match, got %s (aspect=%.2f)", got.Path, TipAspect(got.Sample.TipW, got.Sample.TipH))
	}
}

func TestTipAspect(t *testing.T) {
	if TipAspect(40, 80) != 0.5 {
		t.Fatalf("got %v", TipAspect(40, 80))
	}
	if TipAspect(0, 80) != 0 {
		t.Fatal("zero width must be unknown")
	}
}

func TestStretchActions(t *testing.T) {
	in := []Action{{At: 100, Pos: 10}, {At: 600, Pos: 90}, {At: 1100, Pos: 20}}
	out := StretchActions(in, 2000)
	if len(out) != 3 {
		t.Fatalf("len=%d", len(out))
	}
	if out[0].At != 0 {
		t.Fatalf("start=%d", out[0].At)
	}
	if out[2].At != 2000 {
		t.Fatalf("end=%d", out[2].At)
	}
	if out[1].Pos != 90 {
		t.Fatalf("pos mutated: %d", out[1].Pos)
	}
}
