// Package pluginhost is the narrow SamNPlayer host surface for drop-folder
// plugin packs (docs/PLUGIN_SYSTEM.md).
//
// Stage H1 (infra): Slot + drop-folder discovery
// (samn-plugin.json) + optional OnFrame Tick wiring. Virtual Person product
// (#264) is cancelled / out of scope — this package keeps the generic
// handshake only. No marketplace, no dynamic Go .so, no Everyday CSRT
// changes.
package pluginhost

import (
	"fmt"
	"sync"
	"sync/atomic"
)

// Stage is the host maturity marker (H1 = drop-folder + optional OnFrame tick).
const Stage = "H1"

// Host is the narrow surface a future in-process plugin may use from SamNPlayer.
// Names match docs/PLUGIN_SYSTEM.md.
type Host interface {
	NowMs() int64
	EmitAnimation(pose PoseSample)
}

// PoseSample is a minimal animation snapshot for a future frontend event bridge.
type PoseSample struct {
	AtMs   int64   `json:"atMs"`
	Stroke float64 `json:"stroke"`
	Vibe   float64 `json:"vibe"`
	Suck   float64 `json:"suck"`
}

// Status reports a plugin-host slot for Settings / API.
type Status struct {
	Enabled     bool   `json:"enabled"`
	Running     bool   `json:"running"`
	Stage       string `json:"stage"`
	Ticks       uint64 `json:"ticks"`
	Message     string `json:"message"`
	PackID      string `json:"packId,omitempty"`
	PackName    string `json:"packName,omitempty"`
	PackVersion string `json:"packVersion,omitempty"`
	PackRoot    string `json:"packRoot,omitempty"`
	PluginsDir  string `json:"pluginsDir,omitempty"`
	PackFound   bool   `json:"packFound"`
}

// Slot is a single in-process plugin-host slot (infra).
// Tick is a no-op when not running so playback stays unaffected.
// Tip ships discovery/install only — Virtual Person product Enable UI is
// cancelled / out of scope (#264 closed).
type Slot struct {
	mu         sync.Mutex
	enabled    bool
	running    bool
	clockMs    atomic.Int64
	ticks      atomic.Uint64
	lastEmit   PoseSample
	pack       *Pack
	pluginsDir string
}

// NewSlot returns an idle plugin-host slot.
func NewSlot() *Slot {
	return &Slot{}
}

// SetPluginsDir records where drop-in packs live (for Status / UI).
func (s *Slot) SetPluginsDir(dir string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.pluginsDir = dir
}

// BindPack attaches a discovered pack (may be nil to clear).
func (s *Slot) BindPack(p *Pack) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.pack = p
}

// BoundPack returns a copy of the bound pack, if any.
func (s *Slot) BoundPack() (Pack, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.pack == nil {
		return Pack{}, false
	}
	return *s.pack, true
}

// Status builds the UI/API view.
func (s *Slot) Status() Status {
	s.mu.Lock()
	defer s.mu.Unlock()
	st := Status{
		Enabled:    s.enabled,
		Running:    s.running,
		Stage:      Stage,
		Ticks:      s.ticks.Load(),
		PluginsDir: s.pluginsDir,
	}
	if s.pack != nil {
		st.PackFound = true
		st.PackID = s.pack.Manifest.ID
		st.PackName = s.pack.Manifest.Name
		st.PackVersion = s.pack.Manifest.Version
		st.PackRoot = s.pack.Root
	}
	switch {
	case s.running && s.pack != nil:
		st.Message = fmt.Sprintf("Plugin host running — pack %s v%s.", s.pack.Manifest.Name, s.pack.Manifest.Version)
	case s.running:
		st.Message = "Plugin host running (H1 — OnFrame tick; no pack bound)."
	case s.enabled:
		st.Message = "Plugin host enabled but not running."
	case !st.PackFound:
		st.Message = "No plugin pack found. Drop a pack folder into Plugins, or use Install pack…"
	default:
		st.Message = fmt.Sprintf("Pack ready: %s v%s.", s.pack.Manifest.Name, s.pack.Manifest.Version)
	}
	return st
}

// Enable arms the host slot. Idempotent when already running. Product Enable UI is cancelled / out of
// scope (Virtual Person #264 closed) — API kept for infra/tests only.
func (s *Slot) Enable() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.enabled = true
	s.running = true
	return nil
}

// Disable stops the host slot. Safe when already idle.
func (s *Slot) Disable() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.enabled = false
	s.running = false
}

// Running reports whether the stub tick path is active.
func (s *Slot) Running() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.running
}

// NowMs returns the last clock sample from Tick (Host surface).
func (s *Slot) NowMs() int64 {
	return s.clockMs.Load()
}

// EmitAnimation records the last pose (Host surface).
func (s *Slot) EmitAnimation(pose PoseSample) {
	s.mu.Lock()
	s.lastEmit = pose
	s.mu.Unlock()
}

// LastEmit returns the last pose passed to EmitAnimation (tests / diagnostics).
func (s *Slot) LastEmit() PoseSample {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.lastEmit
}

// TickCount returns how many Ticks ran while the slot was running.
func (s *Slot) TickCount() uint64 {
	return s.ticks.Load()
}

// Tick advances the stub clock. No-op when not running — playback-safe.
func (s *Slot) Tick(atMs int64, scriptPos, vibe, suck float64) {
	if !s.Running() {
		return
	}
	s.clockMs.Store(atMs)
	s.ticks.Add(1)
	_ = scriptPos
	s.EmitAnimation(PoseSample{AtMs: atMs, Stroke: vibe * 100, Vibe: vibe, Suck: suck})
}
