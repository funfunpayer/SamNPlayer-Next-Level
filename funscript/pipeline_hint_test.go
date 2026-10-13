package funscript

import "testing"

func TestSuggestPipelineTwoROIUsesStroke(t *testing.T) {
	s := SuggestPipeline(80, 80, 40, 40)
	if s.Backend != "csrt" || s.Profile != "standard" || !s.GoPath {
		t.Fatalf("two ROI must suggest Stroke (standard), not tf; got %+v", s)
	}
}

func TestSuggestPipelineSmallUsesCSRTGo(t *testing.T) {
	s := SuggestPipeline(18, 16, 0, 0)
	if s.Backend != "csrt" || !s.GoPath {
		t.Fatalf("small ROI must stay CSRT/Go, got %+v", s)
	}
}

func TestSuggestPipelineLargeUsesCSRT(t *testing.T) {
	s := SuggestPipeline(80, 90, 0, 0)
	if s.Backend != "csrt" || s.Profile != "standard" || !s.GoPath {
		t.Fatalf("got %+v", s)
	}
}
