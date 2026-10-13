package generator

import "github.com/funfunpayer/SamNPlayer/funscript"

// distancePartnersActive is true when Zone 2 / extra targets should drive
// tip↔partner distance tracking. Stroke profiles (standard / weich /
// autotune) always use the tip ROI alone — even if Zone 2 was marked.
// That mismatch (#145) previously ran two-point tracking under Autotune
// filters and often ended in "no discernible motion".
func distancePartnersActive(opts Options) bool {
	if opts.ROI2.W <= 0 || opts.ROI2.H <= 0 {
		return false
	}
	return funscript.IsDistanceProfile(opts.Profile)
}
