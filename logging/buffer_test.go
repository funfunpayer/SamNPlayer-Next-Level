package logging

import (
	"strings"
	"testing"
)

func TestRingRecordsAndCaps(t *testing.T) {
	ClearRecent()
	Info("hello", "k", 1)
	Warn("careful")
	Error("boom", "code", 42)
	got := Recent()
	if len(got) < 3 {
		t.Fatalf("want >=3 entries, got %d", len(got))
	}
	last := got[len(got)-1]
	if last.Level != "ERROR" || !strings.Contains(last.Message, "boom") {
		t.Fatalf("last=%+v", last)
	}
	ClearRecent()
	if len(Recent()) != 0 {
		t.Fatal("clear failed")
	}
}
