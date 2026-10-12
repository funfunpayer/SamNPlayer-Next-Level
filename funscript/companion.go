package funscript

import (
	"errors"
	"io/fs"
	"path/filepath"
	"sort"
	"strings"
)

// Companion axis files (docs/DEVICE_SUPPORT.md, D4): multi-axis scripts
// keep each extra axis in its own funscript beside the main one, named
// "<name>.<axis>.funscript" (the convention of OpenFunscripter and
// MultiFunPlayer). The main script stays the stroke (L0).

// CompanionAxes lists the extra axes SamNPlayer reads, in display order.
var CompanionAxes = []AxisName{"surge", "sway", "twist", "roll", "pitch"}

// IsCompanionAxis reports whether name is one of CompanionAxes.
func IsCompanionAxis(name AxisName) bool {
	for _, a := range CompanionAxes {
		if a == name {
			return true
		}
	}
	return false
}

// CompanionPath returns the file of axis next to the main script path
// (".funscript" or ".samn"): /x/clip.funscript → /x/clip.twist.funscript.
func CompanionPath(mainPath string, axis AxisName) string {
	base := mainPath
	if ext := filepath.Ext(base); strings.EqualFold(ext, ".funscript") || strings.EqualFold(ext, ".samn") {
		base = strings.TrimSuffix(base, ext)
	}
	return base + "." + string(axis) + ".funscript"
}

// LoadCompanionAxis reads one companion axis. A missing file is no error:
// it returns nil, nil. Actions come sorted by time, inversion applied.
func LoadCompanionAxis(mainPath string, axis AxisName) ([]Action, error) {
	if mainPath == "" || !IsCompanionAxis(axis) {
		return nil, nil
	}
	s, err := Load(CompanionPath(mainPath, axis))
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return nil, nil
		}
		return nil, err
	}
	acts := append([]Action(nil), s.PlaybackActions()...)
	sort.SliceStable(acts, func(i, j int) bool { return acts[i].At < acts[j].At })
	return acts, nil
}

// CompanionAxesFound lists the companion axes that exist beside mainPath.
func CompanionAxesFound(mainPath string) []AxisName {
	var out []AxisName
	for _, a := range CompanionAxes {
		if acts, err := LoadCompanionAxis(mainPath, a); err == nil && len(acts) > 0 {
			out = append(out, a)
		}
	}
	return out
}
