package simpletrack

import "math"

// tipPartnerDistance measures tip→partner contact distance from the tip box
// point nearest the partner center (not tip center). See trackcv.TipPartnerDistance.
func tipPartnerDistance(tip, partner Rect) float64 {
	pcx := float64(partner.X) + float64(partner.W)/2
	pcy := float64(partner.Y) + float64(partner.H)/2
	tx := clampF(pcx, float64(tip.X), float64(tip.X+tip.W))
	ty := clampF(pcy, float64(tip.Y), float64(tip.Y+tip.H))
	return math.Hypot(pcx-tx, pcy-ty)
}

func clampF(v, lo, hi float64) float64 {
	if lo > hi {
		lo, hi = hi, lo
	}
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// fuseTipPartners mirrors trackcv.FuseTipPartners — exclude lost partners from min().
func fuseTipPartners(tip Rect, tipOK bool, partners []Rect, include []bool) (float64, bool) {
	if !tipOK || len(partners) == 0 {
		return 0, false
	}
	best := 0.0
	any := false
	for i, p := range partners {
		if i < len(include) && !include[i] {
			continue
		}
		d := tipPartnerDistance(tip, p)
		if !any || d < best {
			best = d
			any = true
		}
	}
	return best, any
}
