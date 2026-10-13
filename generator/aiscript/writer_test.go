package aiscript

import "testing"

func TestStatusForEmpty(t *testing.T) {
	st := StatusFor("")
	if st.Available {
		t.Fatal("empty model must not be available")
	}
	if st.Stage != "S1" {
		t.Fatalf("stage=%q", st.Stage)
	}
	if st.Reason == "" {
		t.Fatal("expected English reason")
	}
}

func TestStatusForPathStillClosedWithoutLibrary(t *testing.T) {
	st := StatusFor("/tmp/fake.onnx")
	if st.Available {
		t.Fatal("must refuse ONNX path until inference ships")
	}
	if st.ModelPath != "/tmp/fake.onnx" {
		t.Fatalf("modelPath=%q", st.ModelPath)
	}
	if st.Stage != "S1" {
		t.Fatalf("stage=%q", st.Stage)
	}
}

func TestStatusWithLibraryAvailable(t *testing.T) {
	dir := t.TempDir()
	_, err := ExportImitationSample(dir, ImitationSample{
		Actions:    []Action{{At: 0, Pos: 10}, {At: 800, Pos: 90}, {At: 1600, Pos: 20}},
		DurationMs: 1600,
	})
	if err != nil {
		t.Fatal(err)
	}
	st := StatusWithLibrary("", dir)
	if !st.Available {
		t.Fatalf("want available: %s", st.Reason)
	}
	if st.Stage != "S2-imitation" {
		t.Fatalf("stage=%q", st.Stage)
	}
	if st.SampleCount != 1 {
		t.Fatalf("count=%d", st.SampleCount)
	}
}

func TestDraftFailsClosedWithoutLibrary(t *testing.T) {
	_, err := Draft(DraftRequest{VideoPath: "/tmp/x.mp4"})
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestDraftFromImitation(t *testing.T) {
	dir := t.TempDir()
	pass := true
	_, err := ExportImitationSample(dir, ImitationSample{
		Actions:    []Action{{At: 0, Pos: 5}, {At: 500, Pos: 95}, {At: 1000, Pos: 40}},
		DurationMs: 1000,
		QDPassed:   &pass,
	})
	if err != nil {
		t.Fatal(err)
	}
	res, err := Draft(DraftRequest{
		VideoPath:    "/tmp/clip.mp4",
		ImitationDir: dir,
		DurationMs:   2000,
	})
	if err != nil {
		t.Fatal(err)
	}
	if res.Engine != "imitation-stretch" {
		t.Fatalf("engine=%q", res.Engine)
	}
	if len(res.Actions) != 3 {
		t.Fatalf("actions=%d", len(res.Actions))
	}
	if res.Actions[0].At != 0 || res.Actions[2].At != 2000 {
		t.Fatalf("span %d..%d", res.Actions[0].At, res.Actions[2].At)
	}
	if res.Actions[1].Pos != 95 {
		t.Fatalf("pos=%d", res.Actions[1].Pos)
	}
}
