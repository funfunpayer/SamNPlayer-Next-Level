package main

import (
	"fmt"
	"strings"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/samn"
)

func (a *App) loadScriptDocument(path string) (*funscript.Script, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return nil, fmt.Errorf("no script path")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return nil, err
		}
		return doc.ToFunscript()
	}
	script, err := funscript.Load(path)
	if err != nil {
		return nil, err
	}
	if script.Inverted && !a.honorInverted() {
		// Schalter aus: die Datei wird so abgespielt, wie die Punkte
		// dastehen. Alles, was danach PlaybackActions nutzt (Kurve,
		// Gerät, SAM, Invertieren, .samn-Umwandlung), sieht dasselbe.
		script.Inverted = false
	}
	return script, nil
}

// honorInverted: see prefPlaybackHonorInverted (default on).
func (a *App) honorInverted() bool {
	return a.settings == nil || a.settings.GetBool(prefPlaybackHonorInverted, true)
}

func (a *App) reloadLoadedScript() error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	script, err := a.loadScriptDocument(path)
	if err != nil {
		return err
	}
	a.setLoadedScript(path, script)
	return nil
}

// ExportLoadedFunscript writes a community .funscript (no Neo-2 axes embedded).
func (a *App) ExportLoadedFunscript(destPath string) (string, error) {
	path := a.loadedScriptPath()
	if path == "" {
		return "", fmt.Errorf("no script loaded")
	}
	doc, err := a.documentFromLoaded()
	if err != nil {
		return "", err
	}
	out := strings.TrimSpace(destPath)
	if out == "" {
		out = samn.CompanionFunscriptPath(path)
	}
	if err := doc.ExportFunscript(out); err != nil {
		return "", err
	}
	return out, nil
}

// SaveLoadedAsSamn writes the native .samn (companion path when loaded from funscript).
func (a *App) SaveLoadedAsSamn() (string, error) {
	path := a.loadedScriptPath()
	if path == "" {
		return "", fmt.Errorf("no script loaded")
	}
	doc, err := a.documentFromLoaded()
	if err != nil {
		return "", err
	}
	out := path
	if !samn.IsSamnPath(path) {
		out = samn.CompanionSamnPath(path)
	}
	if err := samn.Save(out, doc); err != nil {
		return "", err
	}
	script, err := doc.ToFunscript()
	if err != nil {
		return "", err
	}
	a.setLoadedScript(out, script)
	return out, nil
}

func (a *App) documentFromLoaded() (*samn.Document, error) {
	path := a.loadedScriptPath()
	script := a.loadedScript()
	if path == "" || script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	if samn.IsSamnPath(path) {
		return samn.Load(path)
	}
	a.stateMu.RLock()
	video := a.videoPath
	a.stateMu.RUnlock()
	d := samn.FromFunscript(script, video)
	if ch, err := funscript.LoadChapters(path); err == nil {
		d.Chapters = ch
	}
	if bm, err := funscript.LoadBookmarks(path); err == nil {
		d.Bookmarks = bm
	}
	if om, err := funscript.LoadOMarkers(path); err == nil {
		d.OMarkers = om
	}
	return d, nil
}

// GetPlaybackSource returns recipe|axes.
func (a *App) GetPlaybackSource() (string, error) {
	script := a.loadedScript()
	if script == nil {
		return "", fmt.Errorf("no script loaded")
	}
	if dr := script.Metadata.DeviceRecipe; dr != nil {
		return funscript.NormalizePlaybackSource(dr.PlaybackSource), nil
	}
	return funscript.PlaybackSourceRecipe, nil
}

// SetPlaybackSource persists recipe|axes.
func (a *App) SetPlaybackSource(source string) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	source = funscript.NormalizePlaybackSource(source)
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		doc.PlaybackSource = source
		doc.Recipe.PlaybackSource = source
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		return a.reloadLoadedScript()
	}
	if err := funscript.SavePlaybackSource(path, source); err != nil {
		return err
	}
	return a.reloadLoadedScript()
}

// GetScriptAxisActions returns general|vibration|suction points.
func (a *App) GetScriptAxisActions(axis string) ([]funscript.Action, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	switch funscript.AxisName(strings.ToLower(strings.TrimSpace(axis))) {
	case funscript.AxisGeneral, "":
		return script.Actions, nil
	case funscript.AxisVibration:
		if script.Metadata.SamnAxes == nil {
			return nil, nil
		}
		return script.Metadata.SamnAxes.Vibration, nil
	case funscript.AxisSuction:
		if script.Metadata.SamnAxes == nil {
			return nil, nil
		}
		return script.Metadata.SamnAxes.Suction, nil
	default:
		// Multi-axis companion files beside the script (D4), read-only.
		if name := funscript.AxisName(strings.ToLower(strings.TrimSpace(axis))); funscript.IsCompanionAxis(name) {
			return funscript.LoadCompanionAxis(a.loadedScriptPath(), name)
		}
		return nil, fmt.Errorf("unknown axis %q", axis)
	}
}

// SaveScriptAxisActions writes one channel to .samn or .funscript.
func (a *App) SaveScriptAxisActions(axis string, actions []funscript.Action) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	name := funscript.AxisName(strings.ToLower(strings.TrimSpace(axis)))
	if name == "" {
		name = funscript.AxisGeneral
	}
	if funscript.IsCompanionAxis(name) {
		return fmt.Errorf("the %s axis is shown read-only (edit it in its own file)", name)
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		cleaned, err := funscript.SanitizeActions(actions)
		if err != nil {
			return err
		}
		switch name {
		case funscript.AxisGeneral:
			doc.General = cleaned
		case funscript.AxisVibration:
			doc.Vibration = cleaned
			doc.PlaybackSource = samn.PlaybackAxes
			doc.Recipe.PlaybackSource = samn.PlaybackAxes
		case funscript.AxisSuction:
			doc.Suction = cleaned
			doc.PlaybackSource = samn.PlaybackAxes
			doc.Recipe.PlaybackSource = samn.PlaybackAxes
		default:
			return fmt.Errorf("unknown axis %q", axis)
		}
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		// Keep the community companion .funscript in sync (curve edit / Invert
		// on .samn). Without this, Share/other apps still saw the pre-edit curve.
		companion := samn.CompanionFunscriptPath(path)
		if err := doc.ExportFunscript(companion); err != nil {
			logging.Warn("app: companion funscript export failed after axis save",
				"path", path, "error", err)
		}
		return a.reloadLoadedScript()
	}
	if err := funscript.SaveAxisActions(path, name, actions); err != nil {
		return err
	}
	return a.reloadLoadedScript()
}

// GetStrengthPresets returns presets and active name (.samn only).
func (a *App) GetStrengthPresets() (map[string]any, error) {
	path := a.loadedScriptPath()
	if path == "" {
		return nil, fmt.Errorf("no script loaded")
	}
	if !samn.IsSamnPath(path) {
		return map[string]any{"presets": []samn.StrengthPreset{}, "active": ""}, nil
	}
	doc, err := samn.Load(path)
	if err != nil {
		return nil, err
	}
	return map[string]any{"presets": doc.StrengthPresets, "active": doc.ActiveStrength}, nil
}

// SetActiveStrength selects a named preset on the loaded .samn.
func (a *App) SetActiveStrength(name string) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	if !samn.IsSamnPath(path) {
		return fmt.Errorf("strength presets require a .samn script")
	}
	doc, err := samn.Load(path)
	if err != nil {
		return err
	}
	doc.ActiveStrength = strings.TrimSpace(name)
	if err := samn.Save(path, doc); err != nil {
		return err
	}
	return a.reloadLoadedScript()
}

// BakeNeoAxesOnLoaded bakes vibe/suction axes and saves .samn (+ funscript export).
func (a *App) BakeNeoAxesOnLoaded() (string, error) {
	doc, err := a.documentFromLoaded()
	if err != nil {
		return "", err
	}
	if err := doc.BakeNeoAxes(); err != nil {
		return "", err
	}
	if len(doc.StrengthPresets) == 0 {
		doc.StrengthPresets = samn.DefaultStrengthPresets()
		doc.ActiveStrength = "normal"
	}
	path := a.loadedScriptPath()
	out := path
	if !samn.IsSamnPath(path) {
		out = samn.CompanionSamnPath(path)
	}
	if err := samn.Save(out, doc); err != nil {
		return "", err
	}
	if err := doc.ExportFunscript(samn.CompanionFunscriptPath(out)); err != nil {
		logging.Warn("app: companion funscript export failed after bake",
			"path", out, "error", err)
	}
	script, err := doc.ToFunscript()
	if err != nil {
		return "", err
	}
	a.setLoadedScript(out, script)
	return out, nil
}

func scriptFileFilters() []runtime.FileFilter {
	return []runtime.FileFilter{
		{DisplayName: "Emotion Script (*.samn)", Pattern: "*.samn"},
		{DisplayName: "Emotion Script + other apps", Pattern: "*.samn;*.funscript"},
		{DisplayName: "Other apps (*.funscript)", Pattern: "*.funscript"},
	}
}
