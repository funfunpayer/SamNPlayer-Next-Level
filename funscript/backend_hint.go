package funscript

// SuggestBackend picks a tracker name from ROI size.
// Product path is always CSRT (Go without Python). Weaker/faster research
// backends (flow, grid_lk, region_fusion*) stay CLI-only — measured quality
// first, fewer dependencies (docs/SELF_BUILD.md, docs/ROADMAP.md).
func SuggestBackend(w, h int) string {
	return "csrt"
}
