// Package player runs a resampled funscript/samn curve on a device.Device —
// time-accurate, cancelable, with optional Extended-O.
//
// Mobile share surface: this package (with device/, funscript/, samn/) is the
// intended core for a later iOS/Android *player-only* app. Do not import
// generator/ here. See docs/PLATFORMS.md.
package player

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
)

// Player spielt eine Frame-Liste in Echtzeit ab.
type Player struct {
	Device device.Device
	// LogEvery gibt an, wie oft OnFrame-Statuszeilen erzeugt werden
	// (0 = aus, sonst z.B. alle 1s). Gilt unabhängig für die
	// Printf-Konsolenausgabe und den OnLog-Callback.
	LogEvery time.Duration

	// OnFrame wird nach jedem gesendeten Frame aufgerufen (z.B. für eine
	// Fortschrittsanzeige in einer GUI). Optional, darf nil sein. Läuft im
	// selben Goroutine wie Play() - bei GUI-Nutzung also selbst per
	// fyne.Do() o.ä. auf den UI-Thread wechseln.
	OnFrame func(f funscript.Frame)

	// OnLog wird zusätzlich zur Konsolenausgabe für Statuszeilen aufgerufen
	// (Start/Ende von Extended-O, periodische Fortschrittszeilen). Optional.
	OnLog func(line string)

	// PauseVideo/ResumeVideo sind optionale Hooks (historisch für Extended-O
	// mit Video-Pause). Extended-O pausiert das Video nicht mehr - die Kurve
	// läuft weiter, nur die Amplitude sinkt - die Hooks bleiben für
	// mögliche andere Nutzung erhalten und werden vom Player nicht mehr
	// selbst aufgerufen.
	PauseVideo  func()
	ResumeVideo func()

	// SoftStartMs: >0 aktiviert sanftes Hochfahren beim Start von Play()
	// bzw. beim ersten Frame in Sync() - statt direkt auf den vollen
	// Skriptwert zu springen, wird über diese Dauer in kleinen Schritten
	// von 0 dorthin gerampt. Vermeidet einen spürbaren Ruck beim Loslegen.
	// 0 = aus (Standardverhalten wie bisher).
	SoftStartMs int

	// StartAtMs: script time Play() starts from (resume after Pause in the
	// GUI without video). Frames before it are skipped and the clock starts
	// there. 0 = from the beginning.
	StartAtMs int64

	lastSyncPosMs int64

	// Stroke output for linear devices (stroke.go). strokeNext: index of
	// the action the device is moving to, -1 = none sent.
	stroke     []funscript.Action
	strokeNext int
	// Extra axes for multi-axis devices (axes.go).
	axes map[string]*axisTrack

	// Playback speed (V1: 0.5×–2×). The clock maps real time to script
	// time as scriptMs = anchorScriptMs + (now - anchorReal) × speed, so a
	// speed change mid-play re-anchors at the current script position and
	// nothing jumps. Only used by Play(); Sync() follows the video clock,
	// whose playbackRate already carries the speed.
	clockMu        sync.Mutex
	speed          float64
	anchorReal     time.Time
	anchorScriptMs float64

	extendedOCh chan ExtendedOOptions

	intensityMu sync.Mutex
	// intensityCut ist die Absenkung durch Extended-O (Faktor = 1 - Cut).
	// Nullwert = volle Kurvenhöhe, auch für einen Player ohne New(); so
	// bleibt MinLevel 0 wirklich 0 statt als "nicht gesetzt" zu gelten.
	intensityCut float64
	eoBusy       atomic.Bool

	// Per-channel output level (V1 Inspector → Devices): stored as a cut so
	// the zero value means full output, also for a Player built without
	// New(). Level = 1 - cut; 0 switches the channel off.
	vibCut float64
	sucCut float64
	// peakReduce: soft limiter on vibration / suction (Effects → Reduce peaks).
	peakReduce bool
	// Stroke output settings (stroke.go), zero value = on, full range:
	// strokeOff switches positions off; the device moves only between
	// strokeLo and 1 - strokeHiCut (safety range, Inspector → Devices).
	strokeOff   bool
	strokeLo    float64
	strokeHiCut float64
}

// Reduce peaks: values up to PeakKnee pass unchanged, everything above is
// compressed by PeakRatio, so a full-strength burst (1.0) comes out at 0.76.
const (
	PeakKnee  = 0.6
	PeakRatio = 0.4
)

// ReducePeak applies the Reduce peaks soft limiter to one output value (0–1).
func ReducePeak(v float64) float64 {
	if v <= PeakKnee {
		return v
	}
	return PeakKnee + (v-PeakKnee)*PeakRatio
}

// SetPeakReduce turns the Reduce peaks limiter on or off, also while playing.
func (p *Player) SetPeakReduce(on bool) {
	p.intensityMu.Lock()
	p.peakReduce = on
	p.intensityMu.Unlock()
}

// PeakReduce reports whether Reduce peaks is on.
func (p *Player) PeakReduce() bool {
	p.intensityMu.Lock()
	defer p.intensityMu.Unlock()
	return p.peakReduce
}

func New(dev device.Device) *Player {
	return &Player{
		Device:      dev,
		extendedOCh: make(chan ExtendedOOptions, 1),
		strokeNext:  -1,
	}
}

// SetChannelLevels sets the vibration / suction output levels (0–1), also
// while playing. Values outside 0–1 are clamped.
func (p *Player) SetChannelLevels(vibration, suction float64) {
	clamp := func(v float64) float64 {
		if !(v >= 0) {
			return 0
		}
		if v > 1 {
			return 1
		}
		return v
	}
	p.intensityMu.Lock()
	p.vibCut = 1 - clamp(vibration)
	p.sucCut = 1 - clamp(suction)
	p.intensityMu.Unlock()
}

// ChannelLevels returns the vibration / suction output levels (0–1).
func (p *Player) ChannelLevels() (vibration, suction float64) {
	p.intensityMu.Lock()
	defer p.intensityMu.Unlock()
	return 1 - p.vibCut, 1 - p.sucCut
}

// MinSpeed / MaxSpeed bound SetSpeed (V1 transport 0.5×–2×).
const (
	MinSpeed = 0.5
	MaxSpeed = 2.0
)

// ClampSpeed limits s to [MinSpeed, MaxSpeed]; 0 / NaN mean 1×.
func ClampSpeed(s float64) float64 {
	if !(s > 0) {
		return 1
	}
	if s < MinSpeed {
		return MinSpeed
	}
	if s > MaxSpeed {
		return MaxSpeed
	}
	return s
}

// SetSpeed changes the playback speed, also while Play() runs.
func (p *Player) SetSpeed(s float64) {
	s = ClampSpeed(s)
	p.clockMu.Lock()
	defer p.clockMu.Unlock()
	if p.speed <= 0 {
		p.speed = 1
	}
	if !p.anchorReal.IsZero() {
		now := time.Now()
		p.anchorScriptMs += float64(now.Sub(p.anchorReal).Milliseconds()) * p.speed
		p.anchorReal = now
	}
	p.speed = s
}

// Speed returns the current playback speed (1 when unset).
func (p *Player) Speed() float64 {
	p.clockMu.Lock()
	defer p.clockMu.Unlock()
	if p.speed <= 0 {
		return 1
	}
	return p.speed
}

func (p *Player) startClock(at time.Time) {
	p.clockMu.Lock()
	defer p.clockMu.Unlock()
	if p.speed <= 0 {
		p.speed = 1
	}
	p.anchorReal = at
	p.anchorScriptMs = float64(p.StartAtMs)
}

// framesFrom drops the frames before atMs (resume); keeps all when atMs <= 0
// or when nothing would be left.
func framesFrom(frames []funscript.Frame, atMs int64) []funscript.Frame {
	if atMs <= 0 {
		return frames
	}
	for i, f := range frames {
		if f.At >= atMs {
			return frames[i:]
		}
	}
	return frames[len(frames)-1:]
}

// realTimeFor returns the wall time at which script time atMs is due.
func (p *Player) realTimeFor(atMs int64) time.Time {
	p.clockMu.Lock()
	defer p.clockMu.Unlock()
	sp := p.speed
	if sp <= 0 {
		sp = 1
	}
	return p.anchorReal.Add(time.Duration((float64(atMs) - p.anchorScriptMs) / sp * float64(time.Millisecond)))
}

func (p *Player) shiftClock(d time.Duration) {
	p.clockMu.Lock()
	p.anchorReal = p.anchorReal.Add(d)
	p.clockMu.Unlock()
}

func (p *Player) logf(format string, args ...interface{}) {
	line := fmt.Sprintf(format, args...)
	fmt.Println(line)
	logging.Debug("player: " + line)
	if p.OnLog != nil {
		p.OnLog(line)
	}
}

// Play spielt die Frames ab. Blockiert bis zum Ende oder bis ctx
// abgebrochen wird. Am Ende (auch bei Abbruch/Fehler) wird Stop() auf dem
// Device aufgerufen, damit nichts "hängen bleibt".
//
// Wird währenddessen TriggerExtendedO() aufgerufen (typischerweise aus
// einer anderen Goroutine, z.B. einem stdin-Listener oder einem GUI-Button),
// läuft die Skript-Timeline weiter - Extended-O skaliert nur die Amplitude
// von Vibration/Sog (Kurve/Rhythmus bleiben), ohne die Wiedergabe zu
// pausieren oder zu verlängern.
func (p *Player) Play(ctx context.Context, frames []funscript.Frame) error {
	if len(frames) == 0 {
		return fmt.Errorf("player: keine Frames zum Abspielen")
	}
	frames = framesFrom(frames, p.StartAtMs)
	logging.Info("player: Wiedergabe startet", "frames", len(frames), "dauer_ms", frames[len(frames)-1].At, "start_ms", p.StartAtMs)
	defer func() {
		logging.Info("player: Wiedergabe beendet")
		p.setIntensityScale(1)
		p.Device.Stop() //nolint:errcheck // best effort beim Beenden
	}()

	start := time.Now()
	lastLog := start
	p.startClock(start)

	if p.SoftStartMs > 0 && len(frames) > 0 {
		t0 := time.Now()
		if err := p.softStart(ctx, frames[0]); err != nil {
			return err
		}
		p.shiftClock(time.Since(t0)) // Zeitbasis um die Ramp-Dauer verschieben, sonst "hinkt" der Rest der Wiedergabe hinterher und holt hektisch auf
	}

	for _, f := range frames {
		p.drainExtendedO(ctx)

		target := p.realTimeFor(f.At)
		if d := time.Until(target); d > 0 {
			select {
			case <-time.After(d):
			case opts := <-p.extendedOCh:
				// Trigger kam während des Wartens - Amplitude skalieren,
				// Timeline nicht anhalten.
				p.startExtendedO(ctx, opts)
				if d := time.Until(p.realTimeFor(f.At)); d > 0 {
					select {
					case <-time.After(d):
					case <-ctx.Done():
						return ctx.Err()
					}
				}
			case <-ctx.Done():
				return ctx.Err()
			}
		}

		if err := p.setOutput(f); err != nil {
			return fmt.Errorf("player: Ausgabe bei t=%dms: %w", f.At, err)
		}
		if err := p.driveStroke(f.At); err != nil {
			return fmt.Errorf("player: Hub bei t=%dms: %w", f.At, err)
		}
		if err := p.driveAxes(f.At); err != nil {
			return fmt.Errorf("player: Achsen bei t=%dms: %w", f.At, err)
		}

		if p.LogEvery > 0 && time.Since(lastLog) >= p.LogEvery {
			scale := p.getIntensityScale()
			p.logf("t=%6dms  vib=%.2f  suc=%.2f  scale=%.2f", f.At, f.Vibration*scale, f.Suction*scale, scale)
			lastLog = time.Now()
		}

		if ctx.Err() != nil {
			return ctx.Err()
		}
	}
	return nil
}
