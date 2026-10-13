package generator

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func candidateSamn(t *testing.T, marks []funscript.SceneMapMark) string {
	t.Helper()
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []samn.Point{{At: 0, Pos: 50}, {At: 20000, Pos: 60}},
		SceneMap: &funscript.SceneMapData{
			Version: 1,
			Video:   funscript.SceneMapVideo{DurationMs: 20000, Width: 1280, Height: 720},
			Grid:    funscript.SceneMapGrid{Cols: 16, Rows: 9},
			Marks:   marks,
		},
	}
	p := filepath.Join(t.TempDir(), "clip.samn")
	if err := samn.Save(p, doc); err != nil {
		t.Fatal(err)
	}
	return p
}

func boolPtr(b bool) *bool    { return &b }
func int64Ptr(v int64) *int64 { return &v }

func TestImportContactCandidates(t *testing.T) {
	path := candidateSamn(t, []funscript.SceneMapMark{
		{ID: "u1", Kind: "region", Class: "glans", Rect: []int{600, 400, 80, 90}, Author: "user"},
		{ID: ContactCandidatePrefix + "4000", Kind: "region", Class: "contact", Author: "auto",
			Rect: []int{1, 1, 50, 50}, AtMs: int64Ptr(4000), Reviewed: boolPtr(false)},
		{ID: ContactCandidatePrefix + "10000", Kind: "region", Class: "contact", Author: "auto",
			Rect: []int{500, 400, 160, 160}, AtMs: int64Ptr(10000), Reviewed: boolPtr(true)},
	})
	contact := filepath.Join(t.TempDir(), "clip.contact.json")
	if err := os.WriteFile(contact, []byte(`{"version":1,"teachers":["nudenet","vlm:qwen"],"points":[
		{"t_ms":0,"x":0.5,"y":0.75,"agree":2,"source":"nudenet"},
		{"t_ms":500,"x":0.5,"y":0.75,"agree":2,"source":"vlm:qwen"},
		{"t_ms":3000,"x":0.5,"y":0.5,"agree":2,"source":"vlm:qwen","box":[0.4,0.4,0.6,0.7]},
		{"t_ms":5000,"x":0.5,"y":0.5,"agree":1,"source":"nudenet"},
		{"t_ms":9000,"x":0.5,"y":0.5,"agree":2,"source":"nudenet"},
		{"t_ms":15000,"x":0.99,"y":0.99,"agree":2,"source":"nudenet"}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	n, err := ImportContactCandidates(path, contact, ContactCandidateOptions{})
	if err != nil {
		t.Fatal(err)
	}
	// 0 ms (500 ms is within the 2 s gap), 3000 ms (teacher box), 15000 ms
	// (clipped at the frame edge). 5000 has one teacher only; 9000 is next
	// to the confirmed candidate at 10 s.
	if n != 3 {
		t.Fatalf("added %d candidates, want 3", n)
	}
	doc, err := samn.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	byID := map[string]funscript.SceneMapMark{}
	for _, m := range doc.SceneMap.Marks {
		byID[m.ID] = m
	}
	if _, ok := byID[ContactCandidatePrefix+"4000"]; ok {
		t.Error("unconfirmed candidate from the earlier import must be replaced")
	}
	if m := byID[ContactCandidatePrefix+"10000"]; m.Reviewed == nil || !*m.Reviewed {
		t.Error("confirmed candidate must stay")
	}
	if _, ok := byID["u1"]; !ok {
		t.Error("user mark must stay")
	}
	c0 := byID[ContactCandidatePrefix+"0"]
	// No teacher box: 2x2 cells (160 px) around (640, 540).
	if want := []int{560, 460, 160, 160}; !equalInts(c0.Rect, want) {
		t.Errorf("point box = %v, want %v", c0.Rect, want)
	}
	if c0.Author != "auto" || c0.Class != "contact" || c0.Kind != "region" ||
		c0.Reviewed == nil || *c0.Reviewed || c0.AtMs == nil || *c0.AtMs != 0 || c0.Confidence != 1 {
		t.Errorf("candidate fields wrong: %+v", c0)
	}
	if c3 := byID[ContactCandidatePrefix+"3000"]; !equalInts(c3.Rect, []int{512, 288, 256, 216}) {
		t.Errorf("teacher box = %v, want [512 288 256 216]", c3.Rect)
	}
	if c15 := byID[ContactCandidatePrefix+"15000"]; c15.Rect[0]+c15.Rect[2] > 1280 || c15.Rect[1]+c15.Rect[3] > 720 {
		t.Errorf("edge box not clipped: %v", c15.Rect)
	}
	// Unconfirmed candidates must stay out of the P5c YOLO export.
	for _, m := range reviewedYOLOMarks(doc.SceneMap.Marks) {
		if m.Reviewed != nil && !*m.Reviewed {
			t.Errorf("unconfirmed candidate %s reached the training export", m.ID)
		}
	}
}

func TestImportContactCandidatesNeedsSceneMap(t *testing.T) {
	doc := &samn.Document{Version: samn.CurrentVersion, Kind: samn.Kind,
		General: []samn.Point{{At: 0, Pos: 50}, {At: 1000, Pos: 60}}}
	p := filepath.Join(t.TempDir(), "noscene.samn")
	if err := samn.Save(p, doc); err != nil {
		t.Fatal(err)
	}
	contact := filepath.Join(t.TempDir(), "c.json")
	if err := os.WriteFile(contact, []byte(`{"version":1,"points":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := ImportContactCandidates(p, contact, ContactCandidateOptions{}); err == nil {
		t.Fatal("want error without scene map")
	}
}

func equalInts(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
