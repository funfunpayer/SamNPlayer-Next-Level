package funscript

import "testing"

func TestContactMarksHasAreas(t *testing.T) {
	if (&ContactMarks{TipClass: "glans"}).HasAreas() {
		t.Fatal("tip class alone is not an area")
	}
	if !(&ContactMarks{Tip: &ContactMarkBox{W: 8, H: 8}}).HasAreas() {
		t.Fatal("tip box should count")
	}
	cm := &ContactMarks{Primary: &ContactMarkBox{W: 10, H: 10}}
	if !cm.HasAreas() {
		t.Fatal("primary box should count")
	}
}
