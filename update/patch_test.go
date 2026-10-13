package update

import (
	"testing"
)

func TestParsePatchTag(t *testing.T) {
	cases := []struct {
		tag      string
		wantBase string
		wantNum  int
		wantID   string
		ok       bool
	}{
		{"patch-v0.5.36-p1", "0.5.36", 1, "v0.5.36-p1", true},
		{"patch-0.5.36-p2", "0.5.36", 2, "v0.5.36-p2", true},
		{"PATCH-v1.2.3-p10", "1.2.3", 10, "v1.2.3-p10", true},
		{"v0.5.36", "", 0, "", false},
		{"patch-v0.5.36", "", 0, "", false},
		{"patch-v0.5.36-p0", "", 0, "", false},
		{"channel/patches", "", 0, "", false},
	}
	for _, c := range cases {
		base, num, id, ok := ParsePatchTag(c.tag)
		if ok != c.ok || base != c.wantBase || num != c.wantNum || id != c.wantID {
			t.Errorf("ParsePatchTag(%q)=(%q,%d,%q,%v) want (%q,%d,%q,%v)",
				c.tag, base, num, id, ok, c.wantBase, c.wantNum, c.wantID, c.ok)
		}
	}
}

func TestIsPlainVersionTag(t *testing.T) {
	good := []string{"v0.5.36", "0.5.36", "v1.0.0"}
	bad := []string{"", "dev", "v0.5.36-p1", "patch-v0.5.36-p1", "v0.5", "v0.5.36-rc1", "latest"}
	for _, g := range good {
		if !isPlainVersionTag(g) {
			t.Errorf("isPlainVersionTag(%q) = false, want true", g)
		}
	}
	for _, b := range bad {
		if isPlainVersionTag(b) {
			t.Errorf("isPlainVersionTag(%q) = true, want false", b)
		}
	}
}

func TestNormalizeVersionStr(t *testing.T) {
	if got := NormalizeVersionStr("v0.5.36-dev"); got != "0.5.36" {
		t.Fatalf("got %q", got)
	}
	if got := NormalizeVersionStr("0.5.36"); got != "0.5.36" {
		t.Fatalf("got %q", got)
	}
}

func TestSelectHighestUnappliedPatch(t *testing.T) {
	// In-memory selection logic mirrored from CheckLatestPatch without network.
	SetAppliedPatchesForTest(map[string]string{"v0.5.36-p1": "t"})
	defer SetAppliedPatchesForTest(nil)

	releases := []ghReleaseLite{
		{TagName: "v0.5.37"},
		{TagName: "patch-v0.5.36-p1"},
		{TagName: "patch-v0.5.36-p3"},
		{TagName: "patch-v0.5.36-p2"},
		{TagName: "patch-v0.5.35-p9"},
		{TagName: "patch-v0.5.36-p4", Draft: true},
	}
	applied, err := AppliedPatchIDs()
	if err != nil {
		t.Fatal(err)
	}
	var best *Patch
	for _, r := range releases {
		if r.Draft {
			continue
		}
		base, num, id, ok := ParsePatchTag(r.TagName)
		if !ok || NormalizeVersionStr(base) != "0.5.36" || applied[id] {
			continue
		}
		cand := &Patch{ID: id, TagName: r.TagName, BaseVersion: base, PatchNumber: num}
		if best == nil || cand.PatchNumber > best.PatchNumber {
			best = cand
		}
	}
	if best == nil || best.ID != "v0.5.36-p3" {
		t.Fatalf("best=%+v want v0.5.36-p3", best)
	}
}

func TestMarkPatchAppliedRoundTrip(t *testing.T) {
	SetAppliedPatchesForTest(map[string]string{})
	defer SetAppliedPatchesForTest(nil)
	if err := MarkPatchApplied("v0.5.36-p1"); err != nil {
		t.Fatal(err)
	}
	ids, err := AppliedPatchIDs()
	if err != nil {
		t.Fatal(err)
	}
	if !ids["v0.5.36-p1"] {
		t.Fatalf("expected applied: %+v", ids)
	}
}
