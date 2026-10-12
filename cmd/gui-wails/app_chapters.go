package main

import (
	"fmt"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/motionx"
)

func (a *App) SuggestBackend(w, h int) string {
	return funscript.SuggestBackend(w, h)
}

func (a *App) SuggestPipeline(w, h, w2, h2 int) funscript.PipelineSuggestion {
	return funscript.SuggestPipeline(w, h, w2, h2)
}

func (a *App) ScriptChapters() ([]motionx.Chapter, error) {
	script := a.loadedScript()
	path := a.loadedScriptPath()
	if script == nil || len(script.Actions) < 4 {
		return nil, fmt.Errorf("no script loaded")
	}
	points := make([]motionx.Point, 0, len(script.Actions))
	for _, act := range script.Actions {
		points = append(points, motionx.Point{TMs: float64(act.At), Pos: float64(act.Pos)})
	}
	segs := motionx.Classify(points, motionx.ClassifyOptions{})
	auto := motionx.Chapters(segs)

	// Gespeicherte metadata.chapters (OFS-Stil) haben Vorrang als Labels,
	// wenn vorhanden — Auto-Kapitel bleiben Fallback.
	if path != "" {
		if stored, err := a.GetScriptChapterMarks(); err == nil && len(stored) > 0 {
			out := make([]motionx.Chapter, 0, len(stored))
			for _, c := range stored {
				kind := c.Name
				if kind == "" {
					kind = "chapter"
				}
				end := c.EndTime
				if end <= c.StartTime {
					end = c.StartTime + 1
				}
				out = append(out, motionx.Chapter{Kind: kind, StartMs: float64(c.StartTime), EndMs: float64(end)})
			}
			return out, nil
		}
	}
	return auto, nil
}
