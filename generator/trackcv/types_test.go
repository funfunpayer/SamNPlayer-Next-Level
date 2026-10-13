package trackcv

import "testing"

func TestSceneMarkRectAt_static(t *testing.T) {
	m := SceneMark{Rect: Rect{10, 20, 30, 40}}
	got := sceneMarkRectAt(m, 5000)
	if got != m.Rect {
		t.Fatalf("static rect: got %+v want %+v", got, m.Rect)
	}
}

func TestSceneMarkRectAt_nearestPath(t *testing.T) {
	m := SceneMark{
		Rect: Rect{0, 0, 10, 10},
		Path: []MarkSample{
			{Ms: 0, Rect: Rect{0, 0, 10, 10}},
			{Ms: 1000, Rect: Rect{50, 60, 10, 10}},
			{Ms: 2000, Rect: Rect{100, 120, 12, 12}},
		},
	}
	got := sceneMarkRectAt(m, 1100)
	want := Rect{50, 60, 10, 10}
	if got != want {
		t.Fatalf("nearest@1100: got %+v want %+v", got, want)
	}
	got = sceneMarkRectAt(m, 1900)
	want = Rect{100, 120, 12, 12}
	if got != want {
		t.Fatalf("nearest@1900: got %+v want %+v", got, want)
	}
}

func TestCameraExcludeRects_usesPath(t *testing.T) {
	tracked := Rect{200, 200, 40, 40}
	marks := []SceneMark{{
		Kind: "exclude", ID: "knee",
		Rect: Rect{0, 0, 20, 20},
		Path: []MarkSample{
			{Ms: 0, Rect: Rect{0, 0, 20, 20}},
			{Ms: 8000, Rect: Rect{80, 90, 20, 20}},
		},
	}}
	got := cameraExcludeRects(tracked, marks, 8000)
	if len(got) != 2 {
		t.Fatalf("len=%d", len(got))
	}
	if got[1] != (Rect{80, 90, 20, 20}) {
		t.Fatalf("exclude path rect: %+v", got[1])
	}
}

func TestMarkShouldFollow(t *testing.T) {
	if markShouldFollow(SceneMark{Kind: "exclude", Follow: true, Rect: Rect{W: 10, H: 10}}) != true {
		t.Fatal("exclude+follow should track")
	}
	if markShouldFollow(SceneMark{Kind: "exclude", Follow: false, Rect: Rect{W: 10, H: 10}}) {
		t.Fatal("sticky exclude must not track")
	}
}

func TestSceneMarkTimebaseIncludesStartOffset(t *testing.T) {
	startTimeSec := 10.0
	mark := SceneMark{Kind: "exclude", FromMs: 10500, ToMs: 11500}

	atMs := int64(750)
	if startTimeSec > 0 {
		atMs += int64(startTimeSec*1000 + 0.5)
	}
	if !sceneMarkActive(mark, atMs) {
		t.Fatalf("mark should be active at absolute video time %dms", atMs)
	}

	relativeOnly := int64(750)
	if sceneMarkActive(mark, relativeOnly) {
		t.Fatalf("regression guard invalid: mark unexpectedly active at relative time %dms", relativeOnly)
	}
}

func TestSceneMarksShiftedKeepsSentinelAndInput(t *testing.T) {
	in := []SceneMark{
		{ID: "whole", Kind: "exclude"}, // 0/0 = whole clip
		{ID: "timed", Kind: "exclude", FromMs: 65000, ToMs: 70000,
			Path: []MarkSample{{Ms: 65000}, {Ms: 66000}}},
	}
	out := sceneMarksShifted(in, -60000)
	if out[0].FromMs != 0 || out[0].ToMs != 0 {
		t.Fatalf("sentinel moved: %+v", out[0])
	}
	if out[1].FromMs != 5000 || out[1].ToMs != 10000 || out[1].Path[0].Ms != 5000 || out[1].Path[1].Ms != 6000 {
		t.Fatalf("timed mark: %+v", out[1])
	}
	if in[1].FromMs != 65000 || in[1].Path[0].Ms != 65000 {
		t.Fatalf("input mutated: %+v", in[1])
	}
	// A run started 60 s in: the grid's window at relative 7 s is video 67 s,
	// where the timed mark is active.
	if !sceneMarkActive(out[1], 7000) || sceneMarkActive(in[1], 7000) {
		t.Fatal("shifted mark must be active on the grid clock, the unshifted one not")
	}
	if got := sceneMarksShifted(in, 0); &got[0] != &in[0] {
		t.Fatal("zero shift must return the input as is")
	}
}
