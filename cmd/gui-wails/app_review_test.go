package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestReviewGeneratedScriptPolarityAndOzone(t *testing.T) {
	path := filepath.Join(t.TempDir(), "g.funscript")
	body := `{"actions":[`
	for i := 0; i <= 100; i++ {
		pos := 15
		if i >= 90 {
			pos = 88
		}
		if i > 0 {
			body += ","
		}
		body += `{"at":` + itoa(i*100) + `,"pos":` + itoa(pos) + `}`
	}
	body += `]}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	a := NewApp()
	rev, err := a.ReviewGeneratedScript(path)
	if err != nil {
		t.Fatal(err)
	}
	if !rev.Polarity.SuggestInvert {
		t.Fatalf("erste Hälfte niedrig: %+v", rev.Polarity)
	}
	if !rev.OZone.OK {
		t.Fatalf("Ende hoch: %+v", rev.OZone)
	}
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [12]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	return string(b[i:])
}
