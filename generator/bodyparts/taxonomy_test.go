package bodyparts_test

import (
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
)

func TestCanonicalCount(t *testing.T) {
	if len(bodyparts.Canonical) != 9 {
		t.Fatalf("want 9 taxonomy parts, got %d", len(bodyparts.Canonical))
	}
	// Free tags: capacity ≥ taxonomy so e.g. all 9 classes + nipples×2 fit.
	if bodyparts.MaxRegionsPerImage < len(bodyparts.Canonical)+1 {
		t.Fatalf("MaxRegionsPerImage=%d too small for duplicate labels (need ≥%d)",
			bodyparts.MaxRegionsPerImage, len(bodyparts.Canonical)+1)
	}
}

func TestNormalizeAliases(t *testing.T) {
	cases := map[string]string{
		"Brust":      bodyparts.Breasts,
		"breast":     bodyparts.Breasts,
		"eichel":     bodyparts.Glans,
		"Hand":       bodyparts.Hand1,
		"hand_left":  bodyparts.Hand1,
		"hand_2":     bodyparts.Hand2,
		"hand_right": bodyparts.Hand2,
		"nippel":     bodyparts.Nipples,
		"Mund":       bodyparts.Mouth,
		"gesicht":    bodyparts.Face,
		"Vagina":     bodyparts.Vagina,
		"  Penis  ":  bodyparts.Penis,
		"custom_toy": "custom_toy",
	}
	for in, want := range cases {
		got := bodyparts.Normalize(in)
		if got != want {
			t.Errorf("Normalize(%q)=%q want %q", in, got, want)
		}
	}
}

func TestPreferredClassesCSV(t *testing.T) {
	csv := bodyparts.PreferredClassesCSV()
	if csv == "" || !strings.Contains(csv, bodyparts.Mouth) || !strings.Contains(csv, bodyparts.Vagina) {
		t.Fatalf("unexpected CSV: %q", csv)
	}
}

func TestDefaultRole(t *testing.T) {
	if bodyparts.DefaultRole(bodyparts.Glans) != bodyparts.RoleTracked {
		t.Fatal("glans should default tracked")
	}
	if bodyparts.DefaultRole(bodyparts.Nipples) != bodyparts.RoleFixed {
		t.Fatal("nipples should default fixed")
	}
	if bodyparts.DefaultRole(bodyparts.Face) != bodyparts.RoleMask {
		t.Fatal("face should default mask")
	}
}
