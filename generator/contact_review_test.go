package generator

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func TestReviewAutoContactCandidateAcceptReject(t *testing.T) {
	dir := t.TempDir()
	video := filepath.Join(dir, "clip.mp4")
	if err := os.WriteFile(video, []byte("fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	samnPath := samn.CompanionSamnPath(video)
	unrev := false
	at := int64(4000)
	doc := &samn.Document{
		Version: samn.CurrentVersion,
		Kind:    samn.Kind,
		General: []samn.Point{{At: 0, Pos: 50}, {At: 10000, Pos: 60}},
		SceneMap: &funscript.SceneMapData{
			Version: 1,
			Video:   funscript.SceneMapVideo{Width: 640, Height: 360, DurationMs: 10000},
			Grid:    funscript.SceneMapGrid{Cols: 16, Rows: 9},
			Windows: []funscript.SceneMapWindow{{StartMs: 0, EndMs: 8000, ScoreB64: funscript.EncodeSceneMapScore([]uint8{1})}},
			Marks: []funscript.SceneMapMark{
				{
					ID: ContactCandidatePrefix + "4000", Kind: "region", Class: "contact",
					Author: "auto", Rect: []int{10, 20, 40, 40}, AtMs: &at, Reviewed: &unrev,
				},
				{ID: "user1", Kind: "exclude", Author: "user", Rect: []int{1, 1, 10, 10}},
			},
		},
	}
	if err := samn.Save(samnPath, doc); err != nil {
		t.Fatal(err)
	}

	if err := ReviewAutoContactCandidate(video, ContactCandidatePrefix+"4000", true); err != nil {
		t.Fatal(err)
	}
	got, err := samn.Load(samnPath)
	if err != nil {
		t.Fatal(err)
	}
	if len(got.SceneMap.Marks) != 2 {
		t.Fatalf("marks after accept: %d", len(got.SceneMap.Marks))
	}
	var auto *funscript.SceneMapMark
	for i := range got.SceneMap.Marks {
		if got.SceneMap.Marks[i].ID == ContactCandidatePrefix+"4000" {
			auto = &got.SceneMap.Marks[i]
		}
	}
	if auto == nil || auto.Reviewed == nil || !*auto.Reviewed {
		t.Fatalf("accept must set reviewed:true: %+v", auto)
	}

	if err := ReviewAutoContactCandidate(video, ContactCandidatePrefix+"4000", false); err != nil {
		t.Fatal(err)
	}
	got2, err := samn.Load(samnPath)
	if err != nil {
		t.Fatal(err)
	}
	for _, m := range got2.SceneMap.Marks {
		if m.ID == ContactCandidatePrefix+"4000" {
			t.Fatalf("reject must delete candidate, still have %+v", m)
		}
	}
	if len(got2.SceneMap.Marks) != 1 || got2.SceneMap.Marks[0].ID != "user1" {
		t.Fatalf("user mark must stay: %+v", got2.SceneMap.Marks)
	}
}

func TestSceneMarkAtMsRoundTrip(t *testing.T) {
	at := int64(2500)
	unrev := false
	marks := []SceneMark{{
		ID: "teacher-contact-2500", Kind: "region",
		Rect: ROI{X: 5, Y: 6, W: 7, H: 8}, Class: "contact", Author: "auto",
		AtMs: &at, Reviewed: &unrev, Confidence: 0.5,
	}}
	meta := buildSceneMapMeta("", 9000, SceneMapDTO{
		Version: 1, Cols: 4, Rows: 2, Width: 100, Height: 50,
		Windows: []MapWindowDTO{{StartMs: 0, EndMs: 8000, Score: []uint8{1}}},
	}, marks)
	if meta == nil || len(meta.Marks) != 1 || meta.Marks[0].AtMs == nil || *meta.Marks[0].AtMs != 2500 {
		t.Fatalf("persist AtMs: %+v", meta)
	}
	_, got, err := SceneMapFromPersist(meta)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || got[0].AtMs == nil || *got[0].AtMs != 2500 {
		t.Fatalf("load AtMs: %+v", got)
	}
	b, _ := json.Marshal(got[0])
	if !strings.Contains(string(b), `"atMs":2500`) {
		t.Fatalf("json missing atMs: %s", b)
	}
}
