package funscript

import "testing"

func TestShiftActionsLaterAndEarlier(t *testing.T) {
	in := []Action{{At: 0, Pos: 10}, {At: 100, Pos: 90}, {At: 300, Pos: 20}}
	later, d := ShiftActions(in, 250)
	if d != 0 || later[0].At != 250 || later[2].At != 550 || later[1].Pos != 90 {
		t.Fatalf("later: %+v dropped %d", later, d)
	}
	earlier, d := ShiftActions(in, -150)
	if d != 2 || len(earlier) != 1 || earlier[0].At != 150 {
		t.Fatalf("earlier: %+v dropped %d (points before 0 must be counted)", earlier, d)
	}
	if in[0].At != 0 {
		t.Fatal("input modified")
	}
}

func TestShiftBookmarksAndChapters(t *testing.T) {
	bms, d := ShiftBookmarks([]Bookmark{{Name: "a", Time: 100}, {Name: "b", Time: 5000}}, -200)
	if d != 1 || len(bms) != 1 || bms[0].Time != 4800 {
		t.Fatalf("bookmarks %+v dropped %d", bms, d)
	}
	chs, d := ShiftChapters([]ChapterMark{
		{Name: "intro", StartTime: 0, EndTime: 1000},   // straddles 0 → clamped
		{Name: "open", StartTime: 100},                 // open-ended, starts < 0 → dropped
		{Name: "main", StartTime: 2000, EndTime: 9000}, // shifted
	}, -500)
	if d != 1 || len(chs) != 2 {
		t.Fatalf("chapters %+v dropped %d", chs, d)
	}
	if chs[0].StartTime != 0 || chs[0].EndTime != 500 || chs[1].StartTime != 1500 || chs[1].EndTime != 8500 {
		t.Fatalf("chapters %+v", chs)
	}
}
