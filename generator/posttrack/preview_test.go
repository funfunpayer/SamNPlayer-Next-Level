package posttrack

import "testing"

func TestPreviewStatsDefaultKnobs(t *testing.T) {
	res, err := PreviewStats(PreviewRequest{
		SmoothWindow:      11,
		MinPeakDistanceMs: 150,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.KeyframeCount < 4 {
		t.Fatalf("expected several keyframes on probe, got %d", res.KeyframeCount)
	}
	if res.PeakCount < 1 {
		t.Fatalf("expected peaks on probe, got %d", res.PeakCount)
	}
	if res.Hint == "" {
		t.Fatal("expected non-empty hint")
	}
	if len(res.Sample) < 2 {
		t.Fatalf("expected sample polyline, got %d", len(res.Sample))
	}
	if res.Sample[0].AtMs > res.Sample[len(res.Sample)-1].AtMs {
		t.Fatalf("sample not time-ordered: %+v", res.Sample)
	}
	if len(res.RawSample) < 2 {
		t.Fatalf("expected raw underlay sample, got %d", len(res.RawSample))
	}
	if res.RawSample[0].AtMs > res.RawSample[len(res.RawSample)-1].AtMs {
		t.Fatalf("raw sample not time-ordered: %+v", res.RawSample)
	}
	if len(res.Peaks) < 1 {
		t.Fatalf("expected peak markers, got %d", len(res.Peaks))
	}
	if len(res.Peaks) > res.PeakCount {
		t.Fatalf("peak markers (%d) exceed PeakCount (%d)", len(res.Peaks), res.PeakCount)
	}
}

func TestPreviewStatsProminenceReducesPeaks(t *testing.T) {
	loose, err := PreviewStats(PreviewRequest{
		SmoothWindow:      11,
		MinPeakDistanceMs: 100,
		PeakProminence:    0,
	})
	if err != nil {
		t.Fatal(err)
	}
	tight, err := PreviewStats(PreviewRequest{
		SmoothWindow:      11,
		MinPeakDistanceMs: 100,
		PeakProminence:    0.45,
	})
	if err != nil {
		t.Fatal(err)
	}
	if tight.PeakCount > loose.PeakCount {
		t.Fatalf("higher prominence should not increase peaks: loose=%d tight=%d",
			loose.PeakCount, tight.PeakCount)
	}
	if tight.KeyframeCount > loose.KeyframeCount {
		t.Fatalf("higher prominence should not increase keyframes: loose=%d tight=%d",
			loose.KeyframeCount, tight.KeyframeCount)
	}
}

func TestPreviewStatsWideSmoothCalmerHint(t *testing.T) {
	res, err := PreviewStats(PreviewRequest{
		SmoothWindow:      25,
		MinPeakDistanceMs: 150,
		PeakProminence:    0.35,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.Hint == "" || !contains(res.Hint, "wide smooth") {
		t.Fatalf("expected wide-smooth hint, got %q", res.Hint)
	}
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (s == sub || len(sub) == 0 ||
		(func() bool {
			for i := 0; i+len(sub) <= len(s); i++ {
				if s[i:i+len(sub)] == sub {
					return true
				}
			}
			return false
		})())
}
