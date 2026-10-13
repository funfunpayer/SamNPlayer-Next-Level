package funscript

import (
	"math"
	"os"
	"path/filepath"
	"testing"
)

func TestBookmarksRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "a.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{"creator":"t"}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	in := []Bookmark{{Name: "A", Time: 100}, {Name: "B", Time: 500}}
	if err := SaveBookmarks(path, in); err != nil {
		t.Fatal(err)
	}
	out, err := LoadBookmarks(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != 2 || out[0].Time != 100 || out[1].Name != "B" {
		t.Fatalf("got %+v", out)
	}
}

func TestBookmarksAcceptFloatSeconds(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "ofs.funscript")
	// OFS-style: fractional seconds in metadata
	raw := `{"actions":[{"at":0,"pos":0},{"at":2000,"pos":100}],"metadata":{"bookmarks":[{"name":"m","time":12.5}],"chapters":[{"name":"c","startTime":1.5,"endTime":3.25}]}}`
	if err := os.WriteFile(path, []byte(raw), 0o644); err != nil {
		t.Fatal(err)
	}
	bm, err := LoadBookmarks(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(bm) != 1 || bm[0].Time != 12500 {
		t.Fatalf("bookmark ms: %+v", bm)
	}
	ch, err := LoadChapters(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(ch) != 1 || ch[0].StartTime != 1500 || ch[0].EndTime != 3250 {
		t.Fatalf("chapters ms: %+v", ch)
	}
}

func TestChaptersRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "a.funscript")
	if err := os.WriteFile(path, []byte(`{"actions":[{"at":0,"pos":10},{"at":2000,"pos":90}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := SaveChapters(path, []ChapterMark{{Name: "Intro", StartTime: 0, EndTime: 500}}); err != nil {
		t.Fatal(err)
	}
	ch, err := LoadChapters(path)
	if err != nil || len(ch) != 1 || ch[0].Name != "Intro" {
		t.Fatalf("got %+v err=%v", ch, err)
	}
}

func TestProjectSidecar(t *testing.T) {
	dir := t.TempDir()
	media := filepath.Join(dir, "clip.mp4")
	path := ProjectPathFor(media)
	if filepath.Base(path) != "clip.snp.json" {
		t.Fatalf("path %s", path)
	}
	p := Project{VideoPath: "clip.mp4", ScriptPath: "clip.funscript", OffsetMs: 40}
	if err := SaveProject(path, p); err != nil {
		t.Fatal(err)
	}
	got, err := LoadProject(path)
	if err != nil || got.OffsetMs != 40 || got.Version != 1 {
		t.Fatalf("got %+v err=%v", got, err)
	}
}

func TestSnapMs(t *testing.T) {
	if SnapMs(20, 25) != 0 && SnapMs(20, 25) != 40 {
		// 20ms at 25fps → frame 0.5 → rounds to 1 → 40ms, or 0
		t.Log(SnapMs(20, 25))
	}
	if SnapMs(100, 0) != 100 {
		t.Fatal("fps 0 should passthrough")
	}
	got := SnapMs(40, 25) // exactly 1 frame
	if got != 40 {
		t.Fatalf("want 40, got %d", got)
	}
}

func TestDeleteRange(t *testing.T) {
	actions := []Action{{At: 0, Pos: 0}, {At: 100, Pos: 50}, {At: 200, Pos: 100}, {At: 300, Pos: 50}}
	out, err := DeleteRange(actions, 100, 200)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != 2 || out[0].At != 0 || out[1].At != 300 {
		t.Fatalf("got %+v", out)
	}
}

func TestCapSpeedRange(t *testing.T) {
	actions := []Action{{At: 0, Pos: 0}, {At: 50, Pos: 100}, {At: 1000, Pos: 100}}
	out, err := CapSpeedRange(actions, 0, 50, 400)
	if err != nil {
		t.Fatal(err)
	}
	// Need dt >= 500*100/400 = 125ms for first segment
	dt := out[1].At - out[0].At
	if dt < 125 {
		t.Fatalf("dt=%d want >=125; out=%+v", dt, out)
	}
}

func TestScaleRangePosFadeSoftEdges(t *testing.T) {
	actions := []Action{
		{At: 0, Pos: 90},
		{At: 50, Pos: 90},
		{At: 100, Pos: 90},
		{At: 200, Pos: 10},
	}
	hard := ScaleRangePos(actions, 0, 100, 0.5)
	soft := ScaleRangePosFade(actions, 0, 100, 0.5, true)
	// Midpoint (50) matches hard scale; edges stay at original (factor ramps to 1).
	if soft[1].Pos != hard[1].Pos {
		t.Fatalf("mid soft=%d hard=%d", soft[1].Pos, hard[1].Pos)
	}
	if soft[0].Pos != 90 {
		t.Fatalf("edge soft=%d want 90 (no scale at edge)", soft[0].Pos)
	}
	if soft[0].Pos <= hard[0].Pos {
		t.Fatalf("edge soft=%d should be closer to original than hard=%d", soft[0].Pos, hard[0].Pos)
	}
}

func TestCapSpeedRangeCapsAllInRangeSegments(t *testing.T) {
	// Three 10ms / 100pos jumps → intensity 5000 each. Cap whole [0,30].
	actions := []Action{
		{At: 0, Pos: 0},
		{At: 10, Pos: 100},
		{At: 20, Pos: 0},
		{At: 30, Pos: 100},
	}
	out, err := CapSpeedRange(actions, 0, 30, 400)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < len(out)-1; i++ {
		dt := float64(out[i+1].At - out[i].At)
		dpos := math.Abs(float64(out[i+1].Pos - out[i].Pos))
		inten := 500.0 * dpos / dt
		if inten > 400.5 {
			t.Fatalf("seg %d intensity=%.1f still over 400; out=%+v", i, inten, out)
		}
	}
}

func TestExportHeatmapPNG(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "h.png")
	actions := []Action{{At: 0, Pos: 0}, {At: 100, Pos: 100}, {At: 500, Pos: 20}, {At: 900, Pos: 80}}
	err := ExportHeatmapPNG(path, actions, HeatmapPNGOptions{
		Width: 120, Height: 24,
		Chapters: []ChapterMark{{Name: "A", StartTime: 100}},
	})
	if err != nil {
		t.Fatal(err)
	}
	st, err := os.Stat(path)
	if err != nil || st.Size() < 50 {
		t.Fatalf("png missing/small: %v %+v", err, st)
	}
}

func containsBytes(b, sub []byte) bool {
	return len(b) >= len(sub) && (string(b) == string(sub) || len(sub) == 0 ||
		(len(b) > 0 && containsString(string(b), string(sub))))
}

func containsString(s, sub string) bool {
	return len(sub) == 0 || (len(s) >= len(sub) && (s == sub || indexOf(s, sub) >= 0))
}

func indexOf(s, sub string) int {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return i
		}
	}
	return -1
}
