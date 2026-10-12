package main

import (
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
	"github.com/wailsapp/wails/v2/pkg/runtime"
)

// GetScriptBookmarks liest metadata.bookmarks (OFS-/Community-Stil) bzw. .samn.
func (a *App) GetScriptBookmarks() ([]funscript.Bookmark, error) {
	path := a.loadedScriptPath()
	if path == "" {
		return nil, fmt.Errorf("no script loaded")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return nil, err
		}
		return doc.Bookmarks, nil
	}
	return funscript.LoadBookmarks(path)
}

// SaveScriptBookmarks schreibt metadata.bookmarks bzw. .samn.
func (a *App) SaveScriptBookmarks(bookmarks []funscript.Bookmark) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		doc.Bookmarks = bookmarks
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		return a.reloadLoadedScript()
	}
	return funscript.SaveBookmarks(path, bookmarks)
}

// GetScriptChapterMarks liest metadata.chapters bzw. .samn.
func (a *App) GetScriptChapterMarks() ([]funscript.ChapterMark, error) {
	path := a.loadedScriptPath()
	if path == "" {
		return nil, fmt.Errorf("no script loaded")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return nil, err
		}
		return doc.Chapters, nil
	}
	return funscript.LoadChapters(path)
}

// SaveScriptChapterMarks schreibt metadata.chapters bzw. .samn.
func (a *App) SaveScriptChapterMarks(chapters []funscript.ChapterMark) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		doc.Chapters = chapters
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		return a.reloadLoadedScript()
	}
	return funscript.SaveChapters(path, chapters)
}

// ExportScriptHeatmapPNG schreibt eine Heatmap-PNG neben das Skript.
func (a *App) ExportScriptHeatmapPNG() (string, error) {
	script := a.loadedScript()
	path := a.loadedScriptPath()
	if script == nil || path == "" {
		return "", fmt.Errorf("no script loaded")
	}
	chapters, _ := a.GetScriptChapterMarks()
	out := path[:len(path)-len(filepath.Ext(path))] + ".heatmap.png"
	err := funscript.ExportHeatmapPNG(out, script.Actions, funscript.HeatmapPNGOptions{
		Width: 960, Height: 56, Chapters: chapters,
	})
	return out, err
}

// SavePlaybackProject schreibt ein .snp.json neben Video oder Skript.
func (a *App) SavePlaybackProject(p funscript.Project) (string, error) {
	media := p.VideoPath
	if media == "" {
		media = p.ScriptPath
	}
	if media == "" {
		media = a.loadedScriptPath()
		p.ScriptPath = media
	}
	if media == "" {
		return "", fmt.Errorf("no video/script for project")
	}
	path := funscript.ProjectPathFor(media)
	p.LastOpenedMs = time.Now().UnixMilli()
	if err := funscript.SaveProject(path, p); err != nil {
		return "", err
	}
	return path, nil
}

// LoadPlaybackProject lädt ein .snp.json.
func (a *App) LoadPlaybackProject(path string) (funscript.Project, error) {
	return funscript.LoadProject(path)
}

// PickPlaybackProject chooses a .snp.json session sidecar (OFS-style project).
func (a *App) PickPlaybackProject() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose project (.snp.json)",
		Filters: []runtime.FileFilter{
			{DisplayName: "Emotion project (*.snp.json)", Pattern: "*.snp.json"},
			{DisplayName: "JSON", Pattern: "*.json"},
		},
	})
}

// SnapTimeMs rastet auf Frame-Raster (fps).
func (a *App) SnapTimeMs(tMs int64, fps float64) int64 {
	return funscript.SnapMs(tMs, fps)
}

func normalizeEditAxis(axis string) string {
	switch funscript.AxisName(strings.ToLower(strings.TrimSpace(axis))) {
	case funscript.AxisVibration:
		return string(funscript.AxisVibration)
	case funscript.AxisSuction:
		return string(funscript.AxisSuction)
	default:
		return string(funscript.AxisGeneral)
	}
}

// EditDeleteRange deletes points in [startMs,endMs] on the active curve axis
// (general | vibration | suction) and saves.
func (a *App) EditDeleteRange(axis string, startMs, endMs int64) error {
	axis = normalizeEditAxis(axis)
	acts, err := a.GetScriptAxisActions(axis)
	if err != nil {
		return err
	}
	if len(acts) == 0 {
		return fmt.Errorf("no %s curve points to edit", axis)
	}
	out, err := funscript.DeleteRange(acts, startMs, endMs)
	if err != nil {
		return err
	}
	return a.SaveScriptAxisActions(axis, out)
}

// EditCapSpeedRange time-stretches too-fast segments in range on the active axis.
func (a *App) EditCapSpeedRange(axis string, startMs, endMs int64, maxIntensity float64) error {
	axis = normalizeEditAxis(axis)
	acts, err := a.GetScriptAxisActions(axis)
	if err != nil {
		return err
	}
	if len(acts) == 0 {
		return fmt.Errorf("no %s curve points to edit", axis)
	}
	out, err := funscript.CapSpeedRange(acts, startMs, endMs, maxIntensity)
	if err != nil {
		return err
	}
	return a.SaveScriptAxisActions(axis, out)
}

// EditScaleRange scales positions in range around 50 on the active axis.
// softEdges ramps the factor (vib/suction-friendly); ignored effect on general
// is still valid — UI enables soft edges mainly for feel channels.
func (a *App) EditScaleRange(axis string, startMs, endMs int64, factor float64, softEdges bool) error {
	axis = normalizeEditAxis(axis)
	acts, err := a.GetScriptAxisActions(axis)
	if err != nil {
		return err
	}
	if len(acts) == 0 {
		return fmt.Errorf("no %s curve points to edit", axis)
	}
	if factor <= 0 {
		factor = 0.8
	}
	out := funscript.ScaleRangePosFade(acts, startMs, endMs, factor, softEdges)
	return a.SaveScriptAxisActions(axis, out)
}
