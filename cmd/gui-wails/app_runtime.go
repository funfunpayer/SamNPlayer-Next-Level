package main

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	goruntime "runtime"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/videox"
)

// RuntimeHealth fasst den Startcheck zusammen: fehlende Ordner wurden
// angelegt (soweit möglich), fehlende optionale/pflichtige Werkzeuge
// werden nur gemeldet - der Start bricht nicht ab.
type RuntimeHealth struct {
	DirsCreated []string         `json:"dirsCreated"`
	DirsFailed  []string         `json:"dirsFailed"`
	Deps        []RuntimeDepInfo `json:"deps"`
	Resources   RuntimeResources `json:"resources"`
	OK          bool             `json:"ok"`
}

// RuntimeResources is the honest machine budget (see docs/PLATFORMS.md).
type RuntimeResources struct {
	NumCPU     int    `json:"numCPU"`
	GOMAXPROCS int    `json:"goMaxProcs"`
	GoOS       string `json:"goos"`
	GoArch     string `json:"goarch"`
}

// RuntimeDepInfo beschreibt ein geprüftes externes Werkzeug.
type RuntimeDepInfo struct {
	ID       string `json:"id"`
	Label    string `json:"label"`
	Required bool   `json:"required"`
	Found    bool   `json:"found"`
	Path     string `json:"path,omitempty"`
	Hint     string `json:"hint,omitempty"`
}

// ensureRuntimeReady legt fehlende Nutzer-Ordner an und prüft Abhängigkeiten
// (ffmpeg Pflicht für Generator/Video, python3 optional). Ergebnis wird
// geloggt und als Event "runtime:health" ans Frontend geschickt.
func (a *App) ensureRuntimeReady() RuntimeHealth {
	h := RuntimeHealth{OK: true}

	for _, dir := range runtimeDirs() {
		if dir == "" {
			continue
		}
		if info, err := os.Stat(dir); err == nil && info.IsDir() {
			continue
		}
		if err := os.MkdirAll(dir, 0o755); err != nil {
			logging.Warn("startup: folder could not be created", "path", dir, "error", err)
			h.DirsFailed = append(h.DirsFailed, dir)
			h.OK = false
			continue
		}
		logging.Info("startup: folder created", "path", dir)
		h.DirsCreated = append(h.DirsCreated, dir)
	}

	h.Deps = checkRuntimeDeps()
	h.Resources = RuntimeResources{
		NumCPU:     goruntime.NumCPU(),
		GOMAXPROCS: goruntime.GOMAXPROCS(0),
		GoOS:       goruntime.GOOS,
		GoArch:     goruntime.GOARCH,
	}
	for _, d := range h.Deps {
		if d.Found {
			logging.Info("startup: dependency ok", "id", d.ID, "path", d.Path)
			continue
		}
		if d.Required {
			logging.Warn("startup: required dependency missing", "id", d.ID, "hint", d.Hint)
			h.OK = false
		} else {
			logging.Info("startup: optional dependency missing", "id", d.ID, "hint", d.Hint)
		}
	}

	if a.ctx != nil {
		runtime.EventsEmit(a.ctx, "runtime:health", h)
	}
	return h
}

// GetRuntimeHealth führt denselben Check erneut aus (z.B. aus den
// Einstellungen nach Installation von ffmpeg).
func (a *App) GetRuntimeHealth() RuntimeHealth {
	videox.ResetToolCache()
	return a.ensureRuntimeReady()
}

// EnsureVideoTools installs ffmpeg/ffprobe into the user tools dir when
// missing (opt-in; downloads a static build). Prefer the portable release
// that already ships ffmpeg next to the GUI binary.
func (a *App) EnsureVideoTools() error {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Minute)
	defer cancel()
	err := videox.EnsureTools(ctx, func(line string) {
		logging.Info("tools: " + line)
		if a.ctx != nil {
			runtime.EventsEmit(a.ctx, "runtime:tools", line)
		}
	})
	videox.ResetToolCache()
	if a.ctx != nil {
		runtime.EventsEmit(a.ctx, "runtime:health", a.ensureRuntimeReady())
	}
	return err
}

func runtimeDirs() []string {
	var dirs []string
	if base, err := os.UserConfigDir(); err == nil {
		root := filepath.Join(base, "SamNPlayer")
		dirs = append(dirs,
			root,
			filepath.Join(root, "logs"),
			filepath.Join(root, "logs", "sessions"),
			filepath.Join(root, "models"),
			filepath.Join(root, "tools"),
			filepath.Join(root, "plugins"),
			filepath.Join(root, "roi_training_dataset"),
			filepath.Join(root, "roi_training_dataset", "images", "train"),
			filepath.Join(root, "roi_training_dataset", "labels", "train"),
		)
	}
	if cache := generator.DefaultCacheDir(); cache != "" {
		dirs = append(dirs, cache)
	}
	if model := defaultAIRoiModelPath(); model != "" {
		dirs = append(dirs, filepath.Dir(model))
	}
	if ds := generator.DefaultRoiDatasetDir(); ds != "" {
		dirs = append(dirs, ds)
	}
	return dirs
}

func checkRuntimeDeps() []RuntimeDepInfo {
	deps := []RuntimeDepInfo{
		{
			ID:       "ffmpeg",
			Label:    "ffmpeg",
			Required: true,
			Hint:     "Needed for video decode / Generate / Make playable. Portable release ships it next to the app; or Settings → Install video tools.",
		},
		{
			ID:       "ffprobe",
			Label:    "ffprobe",
			Required: false,
			Hint:     "Optional metadata helper — usually next to ffmpeg.",
		},
		{
			ID:       "python3",
			Label:    "Python 3",
			Required: false,
			Hint:     "Optional for the classic Python generator and AI training. The Go path does not need Python.",
		},
	}
	if goruntime.GOOS == "windows" {
		deps[2].ID = "python"
		deps[2].Label = "Python"
	}
	videox.ResetToolCache()
	if p, err := videox.FFmpeg(); err == nil {
		deps[0].Found = true
		deps[0].Path = p
	}
	if p, err := videox.FFprobe(); err == nil {
		deps[1].Found = true
		deps[1].Path = p
	}
	name := deps[2].ID
	if path, err := exec.LookPath(name); err == nil {
		deps[2].Found = true
		deps[2].Path = path
	}
	return deps
}
