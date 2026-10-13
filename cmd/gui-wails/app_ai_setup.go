package main

import (
	"context"
	"fmt"
	"strings"
	"sync"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/wailsapp/wails/v2/pkg/runtime"
)

var (
	aiSetupMu        sync.Mutex
	aiSetupBusy      bool
	contactPtsBusy   bool
	contactPtsCancel context.CancelFunc
	contactPtsSeq    uint64
)

func claimAISetupRun() error {
	aiSetupMu.Lock()
	defer aiSetupMu.Unlock()
	if aiSetupBusy {
		return fmt.Errorf("an AI setup install is already running")
	}
	aiSetupBusy = true
	return nil
}

func releaseAISetupRun() {
	aiSetupMu.Lock()
	aiSetupBusy = false
	aiSetupMu.Unlock()
}

func claimContactPointsRun() (context.Context, uint64, error) {
	aiSetupMu.Lock()
	defer aiSetupMu.Unlock()
	if contactPtsBusy {
		return nil, 0, fmt.Errorf("contact points generation is already running")
	}
	if contactPtsCancel != nil {
		contactPtsCancel()
		contactPtsCancel = nil
	}
	contactPtsBusy = true
	contactPtsSeq++
	seq := contactPtsSeq
	ctx, cancel := context.WithCancel(context.Background())
	contactPtsCancel = cancel
	return ctx, seq, nil
}

func releaseContactPointsRun(seq uint64) {
	aiSetupMu.Lock()
	defer aiSetupMu.Unlock()
	if contactPtsSeq == seq {
		contactPtsBusy = false
		if contactPtsCancel != nil {
			contactPtsCancel()
			contactPtsCancel = nil
		}
	}
}

// CancelContactPoints stops an in-flight GenerateContactPointsForVideo run.
// Safe when nothing is running. The worker emits contactpoints:done{cancelled}.
func (a *App) CancelContactPoints() {
	aiSetupMu.Lock()
	cancel := contactPtsCancel
	aiSetupMu.Unlock()
	if cancel != nil {
		cancel()
	}
}

// CheckAISetup runs ai_setup.py check (GPU, packages, train venv, local
// servers + next steps). Changes nothing. Settings "Check AI setup" button.
func (a *App) CheckAISetup() (generator.AISetupReport, error) {
	return generator.CheckAISetup()
}

// InstallAISetup runs ai_setup.py install <profile> --yes asynchronously.
// Events: aisetup:progress (line), aisetup:done {ok|error}. Profiles:
// teachers | train | models | all. Training uses a separate venv so CSRT
// OpenCV is never touched.
func (a *App) InstallAISetup(profile string) error {
	profile = strings.TrimSpace(profile)
	if err := claimAISetupRun(); err != nil {
		return err
	}
	go func() {
		defer releaseAISetupRun()
		err := generator.InstallAISetup(profile, func(line string) {
			runtime.EventsEmit(a.ctx, "aisetup:progress", line)
		})
		if err != nil {
			runtime.EventsEmit(a.ctx, "aisetup:done", map[string]any{"error": err.Error()})
			return
		}
		runtime.EventsEmit(a.ctx, "aisetup:done", map[string]any{"ok": true, "profile": profile})
	}()
	return nil
}

// ContactPointsRunOpts is the Wails-friendly shape for GenerateContactPoints.
type ContactPointsRunOpts struct {
	NudeNet  bool     `json:"nudenet"`
	Teachers []string `json:"teachers"`
	Onnx     []string `json:"onnx"`
	StepS    float64  `json:"stepS"`
	Out      string   `json:"out"`
}

// GenerateContactPointsForVideo runs contact_points.py for the Create video
// (teachers checkboxes). Events: contactpoints:progress, contactpoints:done
// {path|error}. Returns immediately; path arrives on done when ok.
func (a *App) GenerateContactPointsForVideo(videoPath string, opts ContactPointsRunOpts) error {
	videoPath = strings.TrimSpace(videoPath)
	if videoPath == "" {
		return fmt.Errorf("no video selected")
	}
	ctx, seq, err := claimContactPointsRun()
	if err != nil {
		return err
	}
	go func() {
		defer releaseContactPointsRun(seq)
		out, err := generator.GenerateContactPointsWithContext(ctx, videoPath, generator.ContactPointsRun{
			NudeNet:  opts.NudeNet,
			Teachers: opts.Teachers,
			Onnx:     opts.Onnx,
			StepS:    opts.StepS,
			Out:      opts.Out,
		}, func(line string) {
			aiSetupMu.Lock()
			cur := contactPtsSeq
			aiSetupMu.Unlock()
			if cur != seq {
				return
			}
			runtime.EventsEmit(a.ctx, "contactpoints:progress", line)
		})
		aiSetupMu.Lock()
		cur := contactPtsSeq
		aiSetupMu.Unlock()
		if cur != seq {
			return
		}
		if err != nil {
			runtime.EventsEmit(a.ctx, "contactpoints:done", map[string]any{
				"error": err.Error(), "seq": seq, "cancelled": ctx.Err() != nil,
			})
			return
		}
		runtime.EventsEmit(a.ctx, "contactpoints:done", map[string]any{"path": out, "seq": seq})
	}()
	return nil
}
