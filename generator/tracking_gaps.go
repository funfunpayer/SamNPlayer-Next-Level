package generator

import "github.com/funfunpayer/SamNPlayer/funscript"

// trackingGapsFromFlags merges consecutive lost frames into TrackingGap
// windows — port of generate_funscript.tracking_gaps_from_flags.
func trackingGapsFromFlags(timestampsMs []int, lostFlags []bool, mergeGapMs int) []funscript.TrackingGap {
	if len(lostFlags) == 0 || len(timestampsMs) != len(lostFlags) {
		return nil
	}
	any := false
	for _, f := range lostFlags {
		if f {
			any = true
			break
		}
	}
	if !any {
		return nil
	}
	if mergeGapMs <= 0 {
		mergeGapMs = 100
	}
	type gap struct{ start, end int }
	var raw []gap
	var start, end *int
	for i, lost := range lostFlags {
		t := timestampsMs[i]
		if lost {
			if start == nil {
				s, e := t, t
				start, end = &s, &e
			} else {
				*end = t
			}
		} else if start != nil {
			raw = append(raw, gap{*start, *end})
			start, end = nil, nil
		}
	}
	if start != nil {
		raw = append(raw, gap{*start, *end})
	}
	if len(raw) == 0 {
		return nil
	}
	merged := []funscript.TrackingGap{{StartMs: int64(raw[0].start), EndMs: int64(raw[0].end)}}
	for _, g := range raw[1:] {
		prev := &merged[len(merged)-1]
		if int64(g.start)-prev.EndMs <= int64(mergeGapMs) {
			prev.EndMs = int64(g.end)
		} else {
			merged = append(merged, funscript.TrackingGap{StartMs: int64(g.start), EndMs: int64(g.end)})
		}
	}
	return merged
}
