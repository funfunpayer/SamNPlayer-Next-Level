package generator

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
)

// SceneProposal is one window of `scene_roles.py` output (scene
// understanding stage 2): the part that moves most is the primary stroke
// target, the part it moves against is the contact partner, and the pair
// gives the scene type. These are proposals: the user applies them, or
// ApplySceneProposal does when the user switched on "Apply AI setup
// automatically" (Owner 28 Sep; default off, everything shown).
type SceneProposal struct {
	TMs        int64         `json:"t_ms"`
	StartMs    int64         `json:"start_ms"`
	EndMs      int64         `json:"end_ms"`
	SceneType  string        `json:"scene_type"` // blowjob|handjob|titjob|penetration, "" = unknown
	Confidence float64       `json:"confidence"`
	Primary    ROICandidate  `json:"primary"`
	Partner    *ROICandidate `json:"partner,omitempty"`
}

// SceneProposals is a loaded `<clip>.scene.json`.
type SceneProposals struct {
	Width     int             `json:"width"`
	Height    int             `json:"height"`
	Proposals []SceneProposal `json:"proposals"`
}

// LoadSceneProposals reads `<clip>.scene.json` from scene_roles.py.
// Proposals are sorted by time; boxes outside the frame or empty are
// dropped with the proposal (primary) or on their own (partner).
func LoadSceneProposals(path string) (SceneProposals, error) {
	var s SceneProposals
	b, err := os.ReadFile(path)
	if err != nil {
		return s, err
	}
	var raw struct {
		Version int `json:"version"`
		SceneProposals
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		return s, fmt.Errorf("generator: scene proposals %s: %w", path, err)
	}
	if raw.Version != 1 {
		return s, fmt.Errorf("generator: scene proposals %s: unsupported version %d", path, raw.Version)
	}
	if raw.Width <= 0 || raw.Height <= 0 {
		return s, fmt.Errorf("generator: scene proposals %s: missing width/height", path)
	}
	s.Width, s.Height = raw.Width, raw.Height
	in := func(c ROICandidate) bool {
		return c.W > 0 && c.H > 0 && c.X >= 0 && c.Y >= 0 && c.X+c.W <= s.Width && c.Y+c.H <= s.Height
	}
	for _, p := range raw.Proposals {
		if !in(p.Primary) {
			continue
		}
		p.Primary.Index = 1
		if p.Partner != nil {
			if in(*p.Partner) {
				p.Partner.Index = 2
			} else {
				p.Partner = nil
			}
		}
		s.Proposals = append(s.Proposals, p)
	}
	sort.SliceStable(s.Proposals, func(i, j int) bool { return s.Proposals[i].TMs < s.Proposals[j].TMs })
	return s, nil
}

// At returns the proposal whose window covers atMs, else the nearest one by
// window centre; ok is false when there are none.
func (s SceneProposals) At(atMs int64) (SceneProposal, bool) {
	best, bestD := -1, int64(0)
	for i, p := range s.Proposals {
		if p.EndMs > p.StartMs && atMs >= p.StartMs && atMs < p.EndMs {
			return p, true
		}
		d := p.TMs - atMs
		if d < 0 {
			d = -d
		}
		if best < 0 || d < bestD {
			best, bestD = i, d
		}
	}
	if best < 0 {
		return SceneProposal{}, false
	}
	return s.Proposals[best], true
}

// ApplySceneProposal fills what the user left empty from one proposal and
// returns one line per value it set or skipped, for the log and the GUI
// (Owner decision 28 Sep: "Apply AI setup automatically" is an opt-in
// setting, default off, and everything applied is shown).
//
//   - ROI (the primary stroke target) and RegionClass: only when empty.
//   - With withPartner: ROI2 (the contact partner, tracked, not fixed) and
//     RegionClass2, only when ROI2 is empty and the profile is not a
//     Tf/Tj distance profile - there ROI2 would switch the curve source to
//     two-point tracking, so the partner stays a proposal.
//
// Classes are set only when they are canonical body-part ids.
func ApplySceneProposal(roi *ROI, opts *Options, p SceneProposal, withPartner bool) []string {
	var out []string
	where := fmt.Sprintf("scene %q, confidence %.2f, %d-%d ms", p.SceneType, p.Confidence, p.StartMs, p.EndMs)
	box := func(c ROICandidate) string { return fmt.Sprintf("%d,%d,%d,%d", c.X, c.Y, c.W, c.H) }
	if roi != nil && (roi.W <= 0 || roi.H <= 0) {
		*roi = ROI{X: p.Primary.X, Y: p.Primary.Y, W: p.Primary.W, H: p.Primary.H}
		out = append(out, fmt.Sprintf("applied ROI %s = %s (%s)", box(p.Primary), p.Primary.Class, where))
		// Normalize first: teachers / scene_roles may emit aliases
		// (hand_right, left_hand, …) that IsCanonical accepts after
		// Normalize, but RegionClass must store the canonical ID.
		if opts != nil && opts.RegionClass == "" {
			if cls := bodyparts.Normalize(p.Primary.Class); bodyparts.IsCanonical(cls) {
				opts.RegionClass = cls
				out = append(out, "applied region class "+cls)
			}
		}
	}
	if p.Partner == nil {
		return out
	}
	partner := fmt.Sprintf("contact partner %s at %s", p.Partner.Class, box(*p.Partner))
	switch {
	case !withPartner:
		out = append(out, partner+" - proposal only (Apply AI setup automatically is off)")
	case opts == nil:
		out = append(out, partner+" - proposal only")
	case opts.ROI2.W > 0 && opts.ROI2.H > 0:
		out = append(out, partner+" - not applied, ROI2 already set by the user")
	case funscript.IsDistanceProfile(opts.Profile):
		out = append(out, partner+" - not applied: with a Tf/Tj distance profile ROI2 would switch the curve to two-point tracking")
	default:
		opts.ROI2 = ROI{X: p.Partner.X, Y: p.Partner.Y, W: p.Partner.W, H: p.Partner.H}
		opts.ROI2Fixed = false
		out = append(out, fmt.Sprintf("applied ROI2 %s = %s, tracked (%s)", box(*p.Partner), p.Partner.Class, where))
		if opts.RegionClass2 == "" {
			if cls := bodyparts.Normalize(p.Partner.Class); bodyparts.IsCanonical(cls) {
				opts.RegionClass2 = cls
				out = append(out, "applied region class 2 "+cls)
			}
		}
	}
	return out
}
