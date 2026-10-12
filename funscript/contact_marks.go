package funscript

// ContactMarkBox is one optional contact area (nipples / mouth / …)
// stored for feel. Does not drive Everyday tip-CSRT stroke by itself.
type ContactMarkBox struct {
	X     int    `json:"x"`
	Y     int    `json:"y"`
	W     int    `json:"w"`
	H     int    `json:"h"`
	Class string `json:"class,omitempty"`
	Fixed bool   `json:"fixed,omitempty"`
}

// ContactMarks is stamped into metadata.contact_marks when the user marked
// tip/contact classes or areas with Contact vibration. DriveStroke is true
// only for Tf/Tj distance profiles; Everyday stroke keeps it false.
type ContactMarks struct {
	TipClass    string           `json:"tip_class,omitempty"`
	Tip         *ContactMarkBox  `json:"tip,omitempty"`
	Primary     *ContactMarkBox  `json:"primary,omitempty"`
	Extras      []ContactMarkBox `json:"extras,omitempty"`
	DriveStroke bool             `json:"drive_stroke"`
}

// HasAreas reports whether any drawable tip or contact box is present.
func (c *ContactMarks) HasAreas() bool {
	if c == nil {
		return false
	}
	if c.Tip != nil && c.Tip.W > 0 && c.Tip.H > 0 {
		return true
	}
	if c.Primary != nil && c.Primary.W > 0 && c.Primary.H > 0 {
		return true
	}
	for _, e := range c.Extras {
		if e.W > 0 && e.H > 0 {
			return true
		}
	}
	return false
}
