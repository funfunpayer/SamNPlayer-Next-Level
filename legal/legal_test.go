package legal

import (
	"strings"
	"testing"
)

func TestEULAEmbedded(t *testing.T) {
	e := strings.Join(strings.Fields(EULA()), " ") // line breaks do not matter
	for _, want := range []string{"freeware", "not open", "private, non-commercial", EULAVersion, "Donations", "own risk",
		// international clauses (Owner 11 Oct)
		"Severability", "CISG", "habitual residence", "AS IS", "Export and sanctions", "English version prevails"} {
		if !strings.Contains(e, want) {
			t.Errorf("EULA missing %q", want)
		}
	}
	n := ThirdPartyNotices()
	for _, want := range []string{"FFmpeg", "GPL", "OpenCV", "Wails", "Open Font License"} {
		if !strings.Contains(n, want) {
			t.Errorf("notices missing %q", want)
		}
	}
}

func TestDonateLinksOnlyHTTPS(t *testing.T) {
	for _, l := range DonateLinks() {
		if !strings.HasPrefix(l.URL, "https://") || l.Name == "" {
			t.Fatalf("bad donate link %+v", l)
		}
	}
}
