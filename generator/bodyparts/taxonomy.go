// Package bodyparts defines the canonical English body-region taxonomy used
// by AI training, Generate, and Tf/Tj contact mode.
//
// Product rule: class IDs are English snake_case. German legacy labels
// (brust, eichel, …) normalize via AliasOf. Masking multiple regions is a
// first-class goal — see docs/BODY_REGIONS.md.
package bodyparts

import (
	"strings"
)

// Canonical class IDs (stable; used in classes.json / preferred_classes).
const (
	Face    = "face"
	Mouth   = "mouth"
	Breasts = "breasts"
	Nipples = "nipples"
	Hand1   = "hand_1"
	Hand2   = "hand_2"
	Penis   = "penis"
	Glans   = "glans"
	Vagina  = "vagina"
)

// MaxRegionsPerImage is how many free tag boxes may be marked on one training
// frame. Boxes are assigned to taxonomy labels afterward; the same class ID
// may appear more than once (e.g. both nipples). Capacity is above
// len(Canonical) so a full class set plus a second nipple still fits.
const MaxRegionsPerImage = 12

// Role describes how a marked region is used at generate time.
type Role string

const (
	RoleTracked Role = "tracked" // follow with CSRT / two-point tracker
	RoleFixed   Role = "fixed"   // stay at the marked box (static anchor)
	RoleMask    Role = "mask"    // exclude / soft-mask; not a stroke driver
)

// Part is one entry in the product taxonomy.
type Part struct {
	ID          string   `json:"id"`
	Label       string   `json:"label"`
	Description string   `json:"description"`
	Aliases     []string `json:"aliases,omitempty"`
}

// Canonical is the ordered product list (English IDs + UI labels).
var Canonical = []Part{
	{ID: Face, Label: "Face", Description: "Head / face outline", Aliases: []string{"kopf", "gesicht"}},
	{ID: Mouth, Label: "Mouth", Description: "Lips / oral contact target", Aliases: []string{"mund", "lips"}},
	{ID: Breasts, Label: "Breasts", Description: "Breast area (cleavage / tissue)", Aliases: []string{"brust", "breast", "tits"}},
	{ID: Nipples, Label: "Nipples", Description: "Nipple / areola contact point", Aliases: []string{"nipple", "nippel", "brustwarze", "brustwarzen"}},
	{ID: Hand1, Label: "Hand 1", Description: "Primary hand", Aliases: []string{"hand", "hand1", "left_hand", "hand_left"}},
	{ID: Hand2, Label: "Hand 2", Description: "Second hand", Aliases: []string{"hand2", "right_hand", "hand_right"}},
	{ID: Penis, Label: "Penis", Description: "Shaft / body of penis", Aliases: []string{}},
	{ID: Glans, Label: "Glans", Description: "Tip / glans (common Tf/Tj tip)", Aliases: []string{"eichel", "tip"}},
	{ID: Vagina, Label: "Vagina", Description: "Vaginal contact target", Aliases: []string{"pussy", "vulva"}},
}

// IDs returns just the canonical id strings in order.
func IDs() []string {
	out := make([]string, len(Canonical))
	for i, p := range Canonical {
		out[i] = p.ID
	}
	return out
}

// PreferredClassesCSV is the default Settings / AI preferred-classes string.
func PreferredClassesCSV() string {
	return strings.Join(IDs(), ",")
}

// Normalize maps a free-text class name to a canonical ID when possible.
// Unknown names are returned trimmed lower-case (custom classes still work).
func Normalize(name string) string {
	s := strings.TrimSpace(strings.ToLower(name))
	if s == "" {
		return ""
	}
	s = strings.ReplaceAll(s, " ", "_")
	s = strings.ReplaceAll(s, "-", "_")
	for _, p := range Canonical {
		if s == p.ID {
			return p.ID
		}
		for _, a := range p.Aliases {
			if s == strings.ToLower(a) {
				return p.ID
			}
		}
	}
	return s
}

// IsCanonical reports whether id is in the product taxonomy.
func IsCanonical(id string) bool {
	id = Normalize(id)
	for _, p := range Canonical {
		if p.ID == id {
			return true
		}
	}
	return false
}

// ValidRole reports whether r is a known generate role.
func ValidRole(r Role) bool {
	switch r {
	case RoleTracked, RoleFixed, RoleMask, "":
		return true
	default:
		return false
	}
}

// DefaultRole returns the usual role for a class in Tf/Tj / generate.
// Tip-like parts default to tracked; contact targets often fixed; face/mask default mask.
func DefaultRole(classID string) Role {
	switch Normalize(classID) {
	case Penis, Glans, Hand1, Hand2:
		return RoleTracked
	case Nipples, Mouth, Vagina, Breasts:
		return RoleFixed
	case Face:
		return RoleMask
	default:
		return RoleTracked
	}
}
