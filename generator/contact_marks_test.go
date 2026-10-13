package generator

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestBuildContactMarksMetaEmpty(t *testing.T) {
	if buildContactMarksMeta(Options{}) != nil {
		t.Fatal("empty opts should not stamp contact_marks")
	}
}

func TestBuildContactMarksMetaStrokeFeelOnly(t *testing.T) {
	cm := buildContactMarksMeta(Options{
		Profile:      "standard",
		RegionClass:  "glans",
		RegionClass2: "nipples",
		TipROI:       ROI{X: 1, Y: 2, W: 50, H: 60},
		ROI2:         ROI{X: 10, Y: 20, W: 30, H: 40},
		ROI2Fixed:    false,
		ExtraTargets: []NamedROI{{X: 50, Y: 60, W: 20, H: 20, Class: "nipples", Fixed: true}},
	})
	if cm == nil {
		t.Fatal("expected contact_marks")
	}
	if cm.TipClass != "glans" {
		t.Fatalf("tip_class=%q", cm.TipClass)
	}
	if cm.Tip == nil || cm.Tip.W != 50 || cm.Tip.Class != "glans" {
		t.Fatalf("tip=%+v", cm.Tip)
	}
	if cm.Primary == nil || cm.Primary.Class != "nipples" || cm.Primary.W != 30 {
		t.Fatalf("primary=%+v", cm.Primary)
	}
	if len(cm.Extras) != 1 || cm.Extras[0].Class != "nipples" {
		t.Fatalf("extras=%+v", cm.Extras)
	}
	if cm.DriveStroke {
		t.Fatal("Everyday stroke must not drive_stroke from contact marks")
	}
}

func TestBuildContactMarksMetaDistanceDrives(t *testing.T) {
	cm := buildContactMarksMeta(Options{
		Profile: "tj",
		ROI2:    ROI{X: 1, Y: 2, W: 3, H: 4},
	})
	if cm == nil || !cm.DriveStroke {
		t.Fatalf("Tf/Tj should drive_stroke: %+v", cm)
	}
}

func TestBuildContactMarksMetaExtraFollow(t *testing.T) {
	cm := buildContactMarksMeta(Options{
		Profile: "standard",
		TipROI:  ROI{X: 1, Y: 2, W: 50, H: 60},
		ROI2:    ROI{X: 10, Y: 20, W: 30, H: 40},
		// Marks S2: Fixed=false (follow) when tip path recorded.
		ExtraTargets: []NamedROI{{X: 50, Y: 60, W: 20, H: 20, Class: "nipples", Fixed: false}},
	})
	if cm == nil || len(cm.Extras) != 1 {
		t.Fatalf("extras=%+v", cm)
	}
	if cm.Extras[0].Fixed {
		t.Fatal("follow extra must stamp Fixed=false")
	}
	if cm.Extras[0].Class != "nipples" || cm.Extras[0].W != 20 {
		t.Fatalf("extra box=%+v", cm.Extras[0])
	}
}

func TestStampContactMarks(t *testing.T) {
	meta := map[string]any{}
	stampContactMarks(meta, Options{RegionClass: "glans"})
	raw, ok := meta["contact_marks"].(*funscript.ContactMarks)
	if !ok || raw == nil || raw.TipClass != "glans" {
		t.Fatalf("stamp failed: %#v", meta["contact_marks"])
	}
}
