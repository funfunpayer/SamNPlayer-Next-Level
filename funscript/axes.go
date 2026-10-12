package funscript

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"strings"
)

// PlaybackSource selects how Neo 2 channels are driven.
const (
	PlaybackSourceRecipe = "recipe" // derive from general actions (default)
	PlaybackSourceAxes   = "axes"   // drive from metadata.samn_axes
)

// AxisName identifies an editable channel.
type AxisName string

const (
	AxisGeneral   AxisName = "general"
	AxisVibration AxisName = "vibration"
	AxisSuction   AxisName = "suction"
)

// SamnAxes holds optional Neo-2-specific curves. Community tools ignore this
// and keep using top-level actions (general / stroke).
type SamnAxes struct {
	Vibration []Action `json:"vibration,omitempty"`
	Suction   []Action `json:"suction,omitempty"`
}

// NormalizePlaybackSource returns recipe or axes.
func NormalizePlaybackSource(s string) string {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case PlaybackSourceAxes:
		return PlaybackSourceAxes
	default:
		return PlaybackSourceRecipe
	}
}

// HasAxes reports whether any explicit channel is present.
func (a *SamnAxes) HasAxes() bool {
	return a != nil && (len(a.Vibration) > 0 || len(a.Suction) > 0)
}

// Axis returns a copy of the named explicit axis (not general).
func (a *SamnAxes) Axis(name AxisName) []Action {
	if a == nil {
		return nil
	}
	switch name {
	case AxisVibration:
		return append([]Action(nil), a.Vibration...)
	case AxisSuction:
		return append([]Action(nil), a.Suction...)
	default:
		return nil
	}
}

// SetAxis replaces one explicit axis (clamps/sorts like SaveActions).
func (a *SamnAxes) SetAxis(name AxisName, actions []Action) error {
	if a == nil {
		return fmt.Errorf("funscript: samn_axes is nil")
	}
	cleaned, err := SanitizeActions(actions)
	if err != nil {
		return err
	}
	switch name {
	case AxisVibration:
		a.Vibration = cleaned
	case AxisSuction:
		a.Suction = cleaned
	default:
		return fmt.Errorf("funscript: axis %q is not an explicit SamN axis", name)
	}
	return nil
}

// SanitizeActions clamps pos to 0-100, rejects empty/negative time, sorts by at.
func SanitizeActions(actions []Action) ([]Action, error) {
	if len(actions) == 0 {
		return nil, fmt.Errorf("funscript: mindestens ein Punkt wird benötigt")
	}
	sorted := append([]Action(nil), actions...)
	for i := range sorted {
		if sorted[i].At < 0 {
			return nil, fmt.Errorf("funscript: Punkt %d hat eine negative Zeit (%dms)", i, sorted[i].At)
		}
		if sorted[i].Pos < 0 {
			sorted[i].Pos = 0
		} else if sorted[i].Pos > 100 {
			sorted[i].Pos = 100
		}
	}
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].At < sorted[j].At })
	return sorted, nil
}

// SampleAxisPos linearly interpolates an axis at time t (pos 0-100).
// Empty axis → 0.
func SampleAxisPos(actions []Action, t int64) float64 {
	if len(actions) == 0 {
		return 0
	}
	if t <= actions[0].At {
		return float64(actions[0].Pos)
	}
	last := actions[len(actions)-1]
	if t >= last.At {
		return float64(last.Pos)
	}
	// Binary search for segment.
	lo, hi := 0, len(actions)-1
	for lo+1 < hi {
		mid := (lo + hi) / 2
		if actions[mid].At <= t {
			lo = mid
		} else {
			hi = mid
		}
	}
	a, b := actions[lo], actions[hi]
	dt := b.At - a.At
	if dt <= 0 {
		return float64(a.Pos)
	}
	frac := float64(t-a.At) / float64(dt)
	return float64(a.Pos) + frac*float64(b.Pos-a.Pos)
}

// BakeAxesFromRecipe runs the recipe mapper (never axes mode) and stores
// vibration/suction as 0-100 action lists. Sparse: keep a point when the
// rounded pos changes or at least every maxStepMs.
func BakeAxesFromRecipe(actions []Action, opts MapOptions, maxStepMs int64) SamnAxes {
	opts.UseExplicitAxes = false
	opts.VibrationAxis = nil
	opts.SuctionAxis = nil
	s := &Script{Actions: actions}
	frames := s.ToIntensityCurve(opts)
	if maxStepMs <= 0 {
		maxStepMs = 200
	}
	return SamnAxes{
		Vibration: framesToActions(frames, true, maxStepMs),
		Suction:   framesToActions(frames, false, maxStepMs),
	}
}

func framesToActions(frames []Frame, vibration bool, maxStepMs int64) []Action {
	if len(frames) == 0 {
		return []Action{{At: 0, Pos: 0}}
	}
	out := make([]Action, 0, len(frames)/4+2)
	var lastAt int64 = -1 << 62
	lastPos := -1
	for _, f := range frames {
		v := f.Suction
		if vibration {
			v = f.Vibration
		}
		pos := int(math.Round(clamp01(v) * 100))
		if pos < 0 {
			pos = 0
		} else if pos > 100 {
			pos = 100
		}
		if lastPos < 0 || pos != lastPos || f.At-lastAt >= maxStepMs {
			out = append(out, Action{At: f.At, Pos: pos})
			lastAt = f.At
			lastPos = pos
		}
	}
	if len(out) == 0 {
		return []Action{{At: 0, Pos: 0}}
	}
	// Always include the final frame time.
	last := frames[len(frames)-1]
	v := last.Suction
	if vibration {
		v = last.Vibration
	}
	pos := int(math.Round(clamp01(v) * 100))
	if out[len(out)-1].At != last.At || out[len(out)-1].Pos != pos {
		out = append(out, Action{At: last.At, Pos: pos})
	}
	return out
}

// SaveAxisActions writes general actions or one samn_axes channel, preserving
// the rest of the JSON document.
func SaveAxisActions(path string, axis AxisName, actions []Action) error {
	cleaned, err := SanitizeActions(actions)
	if err != nil {
		return err
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}

	if axis == AxisGeneral {
		actionsJSON, err := json.Marshal(cleaned)
		if err != nil {
			return err
		}
		doc["actions"] = actionsJSON
	} else {
		meta := map[string]json.RawMessage{}
		if raw, ok := doc["metadata"]; ok && len(raw) > 0 && string(raw) != "null" {
			if err := json.Unmarshal(raw, &meta); err != nil {
				return fmt.Errorf("funscript: metadata ungültig: %w", err)
			}
		}
		axes := SamnAxes{}
		if raw, ok := meta["samn_axes"]; ok && len(raw) > 0 && string(raw) != "null" {
			_ = json.Unmarshal(raw, &axes)
		}
		if err := axes.SetAxis(axis, cleaned); err != nil {
			return err
		}
		axesJSON, err := json.Marshal(axes)
		if err != nil {
			return err
		}
		meta["samn_axes"] = axesJSON

		// Prefer explicit playback once the user edits a Neo-2 axis.
		recipe := DeviceRecipe{}
		if raw, ok := meta["device_recipe"]; ok && len(raw) > 0 && string(raw) != "null" {
			_ = json.Unmarshal(raw, &recipe)
		}
		recipe.PlaybackSource = PlaybackSourceAxes
		recipeJSON, err := json.Marshal(recipe)
		if err != nil {
			return err
		}
		meta["device_recipe"] = recipeJSON

		metaJSON, err := json.Marshal(meta)
		if err != nil {
			return err
		}
		doc["metadata"] = metaJSON
	}

	out, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(path, out, 0o644)
}

// SavePlaybackSource updates device_recipe.playback_source only.
func SavePlaybackSource(path string, source string) error {
	source = NormalizePlaybackSource(source)
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	meta := map[string]json.RawMessage{}
	if raw, ok := doc["metadata"]; ok && len(raw) > 0 && string(raw) != "null" {
		if err := json.Unmarshal(raw, &meta); err != nil {
			return fmt.Errorf("funscript: metadata ungültig: %w", err)
		}
	}
	recipe := DeviceRecipe{}
	if raw, ok := meta["device_recipe"]; ok && len(raw) > 0 && string(raw) != "null" {
		if err := json.Unmarshal(raw, &recipe); err != nil {
			return fmt.Errorf("funscript: device_recipe ungültig: %w", err)
		}
	}
	recipe.PlaybackSource = source
	recipeJSON, err := json.Marshal(recipe)
	if err != nil {
		return err
	}
	meta["device_recipe"] = recipeJSON
	metaJSON, err := json.Marshal(meta)
	if err != nil {
		return err
	}
	doc["metadata"] = metaJSON
	out, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(path, out, 0o644)
}
