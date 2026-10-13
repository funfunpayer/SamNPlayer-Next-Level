package main

import (
	"context"
	"fmt"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/player"
	"github.com/funfunpayer/SamNPlayer/sam"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// eventsEmit is runtime.EventsEmit. Tests swap it to record what the GUI
// would receive (the Wails runtime itself needs a live app context).
var eventsEmit = runtime.EventsEmit

type PlaybackOptions struct {
	Mock                    bool    `json:"mock"`
	SyncMode                string  `json:"syncMode"`
	TickMs                  int64   `json:"tickMs"`
	MaxSpeed                float64 `json:"maxSpeed"`
	Smoothing               float64 `json:"smoothing"`
	SoftStartMs             int     `json:"softStartMs"`
	UseVideoSync            bool    `json:"useVideoSync"`
	ExtendedOEnabled        bool    `json:"extendedOEnabled"`
	ExtendedOMin            float64 `json:"extendedOMin"`
	ExtendedOHoldS          float64 `json:"extendedOHoldS"`
	ExtendedORestoreMs      float64 `json:"extendedORestoreMs"`
	DisableContactVibration bool    `json:"disableContactVibration"`
	// ContactIntensityScale: live SAM-Korrektur 0–2 (1=Rezept), Datei unverändert.
	ContactIntensityScale float64 `json:"contactIntensityScale"`
	// ContactExtraSmooth: zusätzliche EMA nach dem Mapping (0=aus).
	ContactExtraSmooth float64 `json:"contactExtraSmooth"`
	// ContactVibrationSpan / Curve: Live-Override der Empfindlichkeit/Kurve
	// (0 / "" = aus DeviceRecipe). Datei bleibt unverändert.
	ContactVibrationSpan  float64 `json:"contactVibrationSpan"`
	ContactVibrationCurve string  `json:"contactVibrationCurve"`
	// Speed: playback speed 0.5–2 (V1 transport; 0 = 1×). With video sync the
	// device follows the video clock, whose playbackRate carries the speed.
	Speed float64 `json:"speed"`
	// StartAtMs: resume position (V1 Play / Pause) for playback without video
	// sync; with video sync the video position drives the device anyway.
	StartAtMs int64 `json:"startAtMs"`
}

// ContactPreviewOptions steuert die zweite Kurvenspur inkl. Live-Overrides.
type ContactPreviewOptions struct {
	MaxPoints             int     `json:"maxPoints"`
	ContactVibrationSpan  float64 `json:"contactVibrationSpan"`
	ContactVibrationCurve string  `json:"contactVibrationCurve"`
	ContactIntensityScale float64 `json:"contactIntensityScale"`
	MuteContact           bool    `json:"muteContact"`
}

// SaveContactSettings schreibt Kontakt-Vibration in die Skript-Metadata
// (device_recipe / .samn recipe) und lädt das Skript neu — damit Preview und nächste
// Wiedergabe denselben „wie die Berührung“-Stand nutzen.
// Stroke profiles (standard/weich/autotune) are allowed — Contact vib is the
// product feel layer, not Tf/Tj-only (TFTJ step 6 / Contact-first).
func (a *App) SaveContactSettings(enabled bool, span float64, curve string) error {
	path := a.loadedScriptPath()
	if path == "" {
		return fmt.Errorf("no script loaded")
	}
	script := a.loadedScript()
	if script == nil {
		return fmt.Errorf("no script loaded")
	}
	if !funscript.AllowsContactSettings(script.Metadata.Profile) {
		return fmt.Errorf("contact vibration only for Stroke/Soft/Autotune or Tf/Tj scripts")
	}
	if samn.IsSamnPath(path) {
		doc, err := samn.Load(path)
		if err != nil {
			return err
		}
		doc.ApplyContactRecipe(enabled, span, curve)
		// Keep baked vibration in sync when driving from axes.
		if funscript.NormalizePlaybackSource(doc.PlaybackSource) == samn.PlaybackAxes {
			if err := doc.BakeNeoAxes(); err != nil {
				return err
			}
		}
		if err := samn.Save(path, doc); err != nil {
			return err
		}
		// Keep community export recipe (contact) aligned.
		if err := doc.ExportFunscript(samn.CompanionFunscriptPath(path)); err != nil {
			logging.Warn("app: companion funscript export failed after contact save",
				"path", path, "error", err)
		}
		return a.reloadLoadedScript()
	}
	if err := funscript.SaveContactRecipe(path, enabled, span, curve); err != nil {
		return err
	}
	reloaded, err := funscript.Load(path)
	if err != nil {
		return err
	}
	a.setLoadedScript(path, reloaded)
	return nil
}

func (a *App) StartPlayback(opts PlaybackOptions) error {
	script := a.loadedScript()
	if script == nil {
		return fmt.Errorf("no script loaded")
	}
	mapOpts := funscript.MapOptionsFromScript(script)
	profile := script.Metadata.Profile
	contactOn := false
	// Live contact knobs for distance OR stroke+contact recipes (feel-decouple).
	if funscript.IsDistanceProfile(profile) || mapOpts.ContactVibration {
		if opts.ContactVibrationSpan > 0 {
			mapOpts.ContactVibrationSpan = opts.ContactVibrationSpan
		}
		if opts.ContactVibrationCurve != "" {
			mapOpts.ContactVibrationCurve = opts.ContactVibrationCurve
		}
		if opts.DisableContactVibration {
			mapOpts.ContactVibration = false
		}
		// Live contact knobs only apply in recipe mode; axes mode uses baked curves.
		if mapOpts.UseExplicitAxes {
			contactOn = false
		} else {
			contactOn = mapOpts.ContactVibration
		}
	}
	if opts.TickMs > 0 {
		mapOpts.TickMs = opts.TickMs
	}
	if opts.MaxSpeed > 0 {
		mapOpts.MaxSpeed = opts.MaxSpeed
	}
	if opts.Smoothing > 0 || (!mapOpts.UseExplicitAxes && !funscript.IsDistanceProfile(profile)) {
		mapOpts.Smoothing = opts.Smoothing
	}
	syncMode, err := funscript.ParseSyncMode(opts.SyncMode)
	if err != nil {
		return err
	}
	if !funscript.IsDistanceProfile(profile) || (opts.SyncMode != "" && opts.SyncMode != "independent") {
		if !mapOpts.UseExplicitAxes {
			mapOpts.Sync = syncMode
		}
	}
	vibScale := opts.ContactIntensityScale
	sucScale := 1.0
	presetVibScale := 1.0
	if path := a.loadedScriptPath(); samn.IsSamnPath(path) {
		if doc, err := samn.Load(path); err == nil {
			if p := doc.ActivePreset(); p != nil {
				if vibScale <= 0 {
					vibScale = 1
				}
				vibScale *= p.VibrationScale
				presetVibScale = p.VibrationScale
				sucScale = p.SuctionScale
				// Recipe mode: preset may override contact shape before mapping.
				if !mapOpts.UseExplicitAxes && mapOpts.ContactVibration {
					if p.ContactSpan > 0 {
						mapOpts.ContactVibrationSpan = p.ContactSpan
					}
					if p.ContactCurve != "" {
						mapOpts.ContactVibrationCurve = p.ContactCurve
					}
				}
			}
		}
	}
	// Map after strength presets may have adjusted contact knobs.
	var frames []funscript.Frame
	if contactOn {
		frames = a.contactFrames(script, mapOpts)
	} else {
		frames = script.ToIntensityCurve(mapOpts)
	}
	frames = sam.AdjustDeviceFrames(frames, sam.RuntimeAdjust{
		IntensityScale: vibScale,
		ExtraSmooth:    opts.ContactExtraSmooth,
		MuteContact:    opts.DisableContactVibration || opts.ContactIntensityScale <= 0,
	})
	if sucScale != 1.0 {
		for i := range frames {
			frames[i].Suction = clamp01Playback(frames[i].Suction * sucScale)
		}
	}
	if len(frames) == 0 {
		return fmt.Errorf("script contains no playable actions")
	}
	// Schalter "Vibration nur aus Audio": ersetzt nur die Vibrationsspur.
	frames, audioNote := a.withAudioVibration(frames, mapOpts.TickMs, presetVibScale)
	a.setCurrentFrames(frames)
	dev, reusedDevice := a.claimSessionDevice(opts.Mock)
	p := player.New(dev)
	p.LogEvery = time.Second
	p.SoftStartMs = opts.SoftStartMs
	if opts.StartAtMs > 0 && !opts.UseVideoSync {
		p.StartAtMs = opts.StartAtMs
	}
	p.SetSpeed(opts.Speed)
	// Strokers (The Handy …) get the script's real positions (D2).
	p.SetStroke(script.PlaybackActions())
	// Multi-axis devices (SR6 over TCode) also get the companion files (D4).
	p.SetAxes(companionAxesFor(a.loadedScriptPath()))
	a.stateMu.RLock()
	p.SetChannelLevels(1-a.vibCut, 1-a.sucCut)
	p.SetPeakReduce(a.peakReduce)
	p.SetStrokeOutput(!a.strokeOff, a.strokeLo, 1-a.strokeHiCut)
	a.stateMu.RUnlock()
	p.OnLog = func(line string) { eventsEmit(a.ctx, "playback:log", line) }
	p.OnFrame = func(f funscript.Frame) {
		eventsEmit(a.ctx, "playback:frame", map[string]any{
			"atMs": f.At, "vibration": f.Vibration, "suction": f.Suction, "totalMs": frames[len(frames)-1].At,
		})
	}
	if opts.UseVideoSync {
		p.PauseVideo = func() { eventsEmit(a.ctx, "video:pause") }
		p.ResumeVideo = func() { eventsEmit(a.ctx, "video:resume") }
	}
	// Set session pointers only after the session slot is ours — otherwise a
	// rejected second StartPlayback would replace the live session's player.
	ctx, err := a.tryStartSession()
	if err != nil {
		return err
	}
	a.stateMu.Lock()
	a.activeDevice = dev
	a.activePlayer = p
	a.stateMu.Unlock()
	go func() {
		defer a.endSession()
		if reusedDevice {
			// Bereits über den Geräte-Tab verbunden - nicht erneut
			// verbinden und am Ende NICHT trennen, das bleibt dessen
			// Sache (siehe claimSessionDevice).
			eventsEmit(a.ctx, "playback:log", "Using existing connection from Tools → Device.")
		} else {
			connectCtx, connectCancel := context.WithTimeout(ctx, 20*time.Second)
			defer connectCancel()
			eventsEmit(a.ctx, "playback:log", "Connecting...")
			if err := dev.Connect(connectCtx); err != nil {
				logging.Error("app: connection failed", "error", err)
				eventsEmit(a.ctx, "playback:error", err.Error())
				// failed:true — frontend must NOT auto-advance the playlist
				eventsEmit(a.ctx, "playback:done", map[string]any{"failed": true})
				return
			}
			defer dev.Disconnect()
		}
		eventsEmit(a.ctx, "playback:log", "Playback starting...")
		if audioNote != "" {
			eventsEmit(a.ctx, "playback:log", audioNote)
		} else if contactOn {
			eventsEmit(a.ctx, "playback:log", "Contact vibration active (SAM)")
		}
		var playErr error
		if opts.UseVideoSync {
			positions := make(chan int64, 4)
			a.stateMu.Lock()
			a.videoPositionCh = positions
			a.stateMu.Unlock()
			playErr = p.Sync(ctx, frames, positions)
		} else {
			playErr = p.Play(ctx, frames)
		}
		if playErr != nil && playErr != context.Canceled {
			eventsEmit(a.ctx, "playback:error", playErr.Error())
			eventsEmit(a.ctx, "playback:done", map[string]any{"failed": true})
			return
		}
		eventsEmit(a.ctx, "playback:done", map[string]any{"failed": false})
	}()
	return nil
}

func (a *App) StopPlayback() {
	a.stopSession()
	// Lock() statt RLock(), und zwar über den ganzen close() hinweg: eine
	// laufende ReportVideoPosition() hält für ihren send bereits RLock() -
	// Lock() blockiert also so lange, bis dieser send fertig ist, und kann
	// den Kanal nie schließen, während gleichzeitig darauf gesendet wird
	// ("panic: send on closed channel", reproduziert mit go test -race und
	// ohne -race unter Last).
	a.stateMu.Lock()
	ch := a.videoPositionCh
	a.videoPositionCh = nil
	a.stateMu.Unlock()
	if ch != nil {
		close(ch)
	}
	// Wait until the playback goroutine's endSession runs — otherwise
	// playNextInPlaylist → play() races tryStartSession (sessionActive).
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		a.stateMu.RLock()
		active := a.sessionActive
		a.stateMu.RUnlock()
		if !active {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func (a *App) ReportVideoPosition(ms int64) {
	// RLock() muss den send mit abdecken, nicht nur das Lesen des Kanals -
	// sonst kann StopPlayback() den Kanal zwischen Prüfung und send
	// schließen (siehe Kommentar dort).
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	if a.videoPositionCh == nil {
		return
	}
	ms -= a.scriptOffsetMs
	if ms < 0 {
		ms = 0
	}
	select {
	case a.videoPositionCh <- ms:
	default:
	}
}

// ReportVideoSyncIdle tells Sync the video paused or is buffering so the
// device goes to 0 immediately (without waiting for the stale watchdog).
func (a *App) ReportVideoSyncIdle() {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	if a.videoPositionCh == nil {
		return
	}
	select {
	case a.videoPositionCh <- player.SyncIdleSentinel:
	default:
	}
}

// SetChannelLevels sets the vibration / suction output level (0–1) for the
// running and every following playback (V1 Inspector → Devices: per-channel
// on/off and level). Script files are not changed.
func (a *App) SetChannelLevels(vibration, suction float64) {
	clamp := func(v float64) float64 {
		if !(v >= 0) {
			return 0
		}
		if v > 1 {
			return 1
		}
		return v
	}
	vibration, suction = clamp(vibration), clamp(suction)
	a.stateMu.Lock()
	a.vibCut, a.sucCut = 1-vibration, 1-suction
	p := a.activePlayer
	a.stateMu.Unlock()
	if p != nil {
		p.SetChannelLevels(vibration, suction)
	}
}

// SetPeakReduce turns Effects → Reduce peaks on or off for the running and
// every following playback: vibration and suction above 60 % are softened
// (full strength comes out at 76 %). Script files are not changed.
func (a *App) SetPeakReduce(on bool) {
	a.stateMu.Lock()
	a.peakReduce = on
	p := a.activePlayer
	a.stateMu.Unlock()
	if p != nil {
		p.SetPeakReduce(on)
	}
}

// SetStrokeOutput sets the stroke output for strokers (The Handy …) for the
// running and every following playback: on/off and the safety range the
// device may move in (0–1, at least player.MinStrokeSpan wide). Returns the
// range actually used. Script files are not changed.
func (a *App) SetStrokeOutput(on bool, lo, hi float64) []float64 {
	lo, hi = player.ClampStrokeRange(lo, hi)
	a.stateMu.Lock()
	a.strokeOff = !on
	a.strokeLo, a.strokeHiCut = lo, 1-hi
	p := a.activePlayer
	a.stateMu.Unlock()
	if p != nil {
		p.SetStrokeOutput(on, lo, hi)
	}
	return []float64{lo, hi}
}

// SetPlaybackSpeed changes the speed of a running playback (0.5–2×) and
// returns the clamped value. Without a running session it only reports the
// clamp; the next StartPlayback takes PlaybackOptions.Speed.
func (a *App) SetPlaybackSpeed(speed float64) float64 {
	speed = player.ClampSpeed(speed)
	a.stateMu.RLock()
	p := a.activePlayer
	a.stateMu.RUnlock()
	if p != nil {
		p.SetSpeed(speed)
	}
	return speed
}

func (a *App) TriggerExtendedO(minLevel, holdSeconds, restoreMs float64) {
	a.stateMu.RLock()
	p := a.activePlayer
	a.stateMu.RUnlock()
	if p == nil {
		return
	}
	p.TriggerExtendedO(player.ExtendedOOptions{
		MinLevel:        minLevel,
		HoldDuration:    time.Duration(holdSeconds * float64(time.Second)),
		RestoreDuration: time.Duration(restoreMs * float64(time.Millisecond)),
	})
}

type HeatmapPoint struct {
	AtMs      int64   `json:"atMs"`
	Intensity float64 `json:"intensity"`
}

type CurvePoint struct {
	AtMs int64 `json:"atMs"`
	Pos  int   `json:"pos"`
}

// VibrationCurvePoint is the contact-vibration intensity (0-1) over time —
// second track under the position curve when device_recipe.contact_vibration
// is set. Derived from the same distance/pos signal as playback, so it lines
// up with the video timeline.
type VibrationCurvePoint struct {
	AtMs      int64   `json:"atMs"`
	Vibration float64 `json:"vibration"`
}

func (a *App) GetScriptCurve(maxPoints int) ([]CurvePoint, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	actions := script.PlaybackActions()
	if len(actions) == 0 {
		return nil, fmt.Errorf("script contains no actions")
	}
	if maxPoints < 100 {
		maxPoints = 100
	}
	if len(actions) <= maxPoints {
		out := make([]CurvePoint, len(actions))
		for i, act := range actions {
			out[i] = CurvePoint{AtMs: act.At, Pos: act.Pos}
		}
		return out, nil
	}
	windows := maxPoints / 2
	span := actions[len(actions)-1].At - actions[0].At
	if span <= 0 {
		span = 1
	}
	out := make([]CurvePoint, 0, maxPoints)
	start := 0
	for w := 0; w < windows && start < len(actions); w++ {
		endAt := actions[0].At + span*int64(w+1)/int64(windows)
		minIdx, maxIdx := start, start
		i := start
		for ; i < len(actions) && (actions[i].At <= endAt || i == start); i++ {
			if actions[i].Pos < actions[minIdx].Pos {
				minIdx = i
			}
			if actions[i].Pos > actions[maxIdx].Pos {
				maxIdx = i
			}
		}
		lo, hi := minIdx, maxIdx
		if lo > hi {
			lo, hi = hi, lo
		}
		out = append(out, CurvePoint{AtMs: actions[lo].At, Pos: actions[lo].Pos})
		if hi != lo {
			out = append(out, CurvePoint{AtMs: actions[hi].At, Pos: actions[hi].Pos})
		}
		start = i
	}
	return out, nil
}

// GetVibrationCurve returns the contact-vibration envelope for the loaded
// script (empty if none). Uses the baked-in device_recipe (no live override).
func (a *App) GetVibrationCurve(maxPoints int) ([]VibrationCurvePoint, error) {
	return a.GetVibrationCurvePreview(ContactPreviewOptions{
		MaxPoints: maxPoints, ContactIntensityScale: 1,
	})
}

// GetVibrationCurvePreview applies live Span/Kurve/Stärke-Overrides — gleiche
// Semantik wie StartPlayback, ohne die Datei anzufassen.
func (a *App) GetVibrationCurvePreview(preview ContactPreviewOptions) ([]VibrationCurvePoint, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	dr := script.Metadata.DeviceRecipe
	maxPoints := preview.MaxPoints
	if maxPoints < 50 {
		maxPoints = 50
	}

	// Explicit axes: sample the vibration channel directly.
	if dr != nil && funscript.NormalizePlaybackSource(dr.PlaybackSource) == funscript.PlaybackSourceAxes {
		if script.Metadata.SamnAxes == nil || len(script.Metadata.SamnAxes.Vibration) == 0 {
			return nil, nil
		}
		duration := script.Duration()
		if duration <= 0 {
			return nil, nil
		}
		tick := duration / int64(maxPoints)
		if tick < 10 {
			tick = 10
		}
		scale := preview.ContactIntensityScale
		if scale <= 0 || preview.MuteContact {
			scale = 0
		}
		out := make([]VibrationCurvePoint, 0, maxPoints+1)
		any := false
		for t := int64(0); t <= duration; t += tick {
			v := funscript.SampleAxisPos(script.Metadata.SamnAxes.Vibration, t) / 100.0
			v *= scale
			if v > 0.001 {
				any = true
			}
			out = append(out, VibrationCurvePoint{AtMs: t, Vibration: v})
		}
		if !any {
			return nil, nil
		}
		return out, nil
	}

	if dr == nil || !dr.ContactVibration {
		return nil, nil
	}
	// Stroke + Contact vib scripts use the same depth envelope as Tf/Tj;
	// do not require IsDistanceProfile (TFTJ step 6).
	opts := funscript.RecipeFor(script.Metadata.Profile)
	opts.ContactVibration = true
	opts.ContactVibrationSpan = dr.ContactVibrationSpan
	opts.ContactVibrationCurve = dr.ContactVibrationCurve
	if preview.ContactVibrationSpan > 0 {
		opts.ContactVibrationSpan = preview.ContactVibrationSpan
	}
	if preview.ContactVibrationCurve != "" {
		opts.ContactVibrationCurve = preview.ContactVibrationCurve
	}
	opts.TrackingGaps = append([]funscript.TrackingGap(nil), script.Metadata.TrackingGaps...)
	opts.Smoothing = 0
	opts.ContactVibrationEnvelope = -1
	duration := script.Duration()
	if duration <= 0 {
		return nil, nil
	}
	opts.TickMs = duration / int64(maxPoints)
	if opts.TickMs < 10 {
		opts.TickMs = 10
	}
	frames := a.contactFrames(script, opts)
	scale := preview.ContactIntensityScale
	mute := preview.MuteContact
	if scale <= 0 {
		mute = true
		scale = 1
	}
	frames = sam.AdjustDeviceFrames(frames, sam.RuntimeAdjust{
		IntensityScale: scale,
		MuteContact:    mute,
	})
	out := make([]VibrationCurvePoint, 0, len(frames))
	any := false
	for _, f := range frames {
		if f.Vibration > 0.001 {
			any = true
		}
		out = append(out, VibrationCurvePoint{AtMs: f.At, Vibration: f.Vibration})
	}
	if !any {
		return nil, nil
	}
	return out, nil
}

func (a *App) GetHeatmap(buckets int) ([]HeatmapPoint, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	if buckets < 10 {
		buckets = 10
	}
	duration := script.Duration()
	if duration <= 0 {
		return nil, fmt.Errorf("script has no valid duration")
	}
	opts := funscript.DefaultMapOptions()
	opts.TickMs = duration / int64(buckets)
	if opts.TickMs < 10 {
		opts.TickMs = 10
	}
	frames := script.ToIntensityCurve(opts)
	points := make([]HeatmapPoint, len(frames))
	for i, f := range frames {
		intensity := f.Vibration
		if f.Suction > intensity {
			intensity = f.Suction
		}
		points[i] = HeatmapPoint{AtMs: f.At, Intensity: intensity}
	}
	return points, nil
}

// GetTrajectory returns the MT-Debug tip/partner trajectory for the loaded
// script (nil when the script was not generated with "capture trajectory"
// on, or has none loaded). Frontend maps Width/Height (video-pixel space at
// generation time) onto the currently rendered <video> box.
func (a *App) GetTrajectory() (*funscript.TrajectoryData, error) {
	script := a.loadedScript()
	if script == nil {
		return nil, fmt.Errorf("no script loaded")
	}
	return script.Metadata.Trajectory, nil
}

// contactFrames: bevorzugt vorhandenes .sam-Sidecar mit Kontakt-Intensity,
// sonst Enrich aus dem Funscript. Dünne Sidecars (nur Position) werden
// übersprungen — sonst bliebe Vibration still auf 0.
// Sidecar/Enrich werden vor dem Geräte-Mapping verdichtet (Densify), damit
// Intensity der Classic-Per-Tick-Auswertung entspricht.
func (a *App) contactFrames(script *funscript.Script, mapOpts funscript.MapOptions) []funscript.Frame {
	if path := a.loadedScriptPath(); path != "" {
		if s, err := sam.LoadSidecarIfPresent(path); err == nil && s != nil && sam.HasContactIntensity(s) {
			dense := sam.Densify(s, mapOpts.TickMs, mapOpts)
			if frames := sam.ToDeviceFrames(dense, mapOpts); len(frames) > 0 {
				return frames
			}
		}
	}
	frames := sam.PlaybackFramesFromFunscript(script, mapOpts)
	if len(frames) == 0 {
		return script.ToIntensityCurve(mapOpts)
	}
	return frames
}

func (a *App) SetScriptOffset(ms int64) {
	if ms < -10000 {
		ms = -10000
	}
	if ms > 10000 {
		ms = 10000
	}
	a.stateMu.Lock()
	a.scriptOffsetMs = ms
	path := a.currentScriptPath
	a.stateMu.Unlock()
	if path != "" {
		_ = a.settings.Set(offsetKeyFor(path), ms)
	}
	logging.Info("playback: script offset set", "ms", ms, "script", path)
}

func (a *App) GetScriptOffset() int64 {
	a.stateMu.RLock()
	defer a.stateMu.RUnlock()
	return a.scriptOffsetMs
}

func offsetKeyFor(scriptPath string) string {
	return "playback.offset." + scriptPath
}

func clamp01Playback(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}

// BakeOffsetResult reports what BakeScriptOffset changed.
type BakeOffsetResult struct {
	ShiftedMs int64 `json:"shiftedMs"`
	// Dropped counts points, bookmarks and chapters that would have landed
	// before 0 ms (only possible with a negative offset).
	Dropped int `json:"dropped"`
}

// BakeScriptOffset writes the current Play offset into the script itself
// (Owner UX ask "offset rewrite"): every curve point on every axis, the
// bookmarks and the chapters move by the offset, the file is saved, and the
// offset goes back to 0 — so other players see the corrected timing too.
// Trajectory / tracking-gap / contact-mark metadata are not moved.
func (a *App) BakeScriptOffset() (BakeOffsetResult, error) {
	a.stateMu.RLock()
	offset := a.scriptOffsetMs
	a.stateMu.RUnlock()
	res := BakeOffsetResult{ShiftedMs: offset}
	if a.loadedScriptPath() == "" {
		return res, fmt.Errorf("no script loaded")
	}
	if offset == 0 {
		return res, nil
	}
	for _, axis := range []funscript.AxisName{funscript.AxisGeneral, funscript.AxisVibration, funscript.AxisSuction} {
		acts, err := a.GetScriptAxisActions(string(axis))
		if err != nil {
			return res, err
		}
		if len(acts) == 0 {
			continue
		}
		shifted, dropped := funscript.ShiftActions(acts, offset)
		res.Dropped += dropped
		if len(shifted) == 0 {
			return res, fmt.Errorf("offset %d ms would move every %s point before the start", offset, axis)
		}
		if err := a.SaveScriptAxisActions(string(axis), shifted); err != nil {
			return res, err
		}
	}
	if bms, err := a.GetScriptBookmarks(); err == nil && len(bms) > 0 {
		shifted, dropped := funscript.ShiftBookmarks(bms, offset)
		res.Dropped += dropped
		if err := a.SaveScriptBookmarks(shifted); err != nil {
			return res, err
		}
	}
	if chs, err := a.GetScriptChapterMarks(); err == nil && len(chs) > 0 {
		shifted, dropped := funscript.ShiftChapters(chs, offset)
		res.Dropped += dropped
		if err := a.SaveScriptChapterMarks(shifted); err != nil {
			return res, err
		}
	}
	if err := a.reloadLoadedScript(); err != nil {
		return res, err
	}
	a.SetScriptOffset(0)
	logging.Info("playback: script offset baked into file", "ms", offset, "dropped", res.Dropped)
	return res, nil
}

// companionAxesFor loads the multi-axis companion files beside the script
// (clip.twist.funscript …). Unreadable files are skipped with a log line.
func companionAxesFor(scriptPath string) map[string][]funscript.Action {
	out := map[string][]funscript.Action{}
	if scriptPath == "" {
		return out
	}
	for _, axis := range funscript.CompanionAxes {
		acts, err := funscript.LoadCompanionAxis(scriptPath, axis)
		if err != nil {
			logging.Warn("playback: companion axis skipped", "axis", axis, "error", err)
			continue
		}
		if len(acts) > 0 {
			out[string(axis)] = acts
		}
	}
	return out
}
