package player

import (
	"context"
	"errors"
	"sort"
	"time"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
)

var errNoFrames = errors.New("player: keine Frames zum Abspielen")

// syncStaleAfter: kommt so lange keine Position, steht das Video (Pause
// über die native Steuerung oder Leertaste, Puffern) - das Frontend meldet
// Positionen nur per timeupdate (~250 ms), und das feuert im Stillstand
// nicht. Sync geht dann auf 0, statt den letzten Wert endlos zu halten,
// und folgt ab der nächsten Position wieder dem Skript. var für Tests.
var syncStaleAfter = 1500 * time.Millisecond

// SyncIdleSentinel: Frontend meldet Pause/Buffering sofort (ohne auf
// syncStaleAfter zu warten). Negative Werte sind keine Videoms.
const SyncIdleSentinel int64 = -1

// Sync treibt das Gerät anhand einer EXTERNEN Positionsquelle statt der
// eigenen Uhr von Play() - gedacht für echte Videowiedergabe (z.B. ein
// HTML5-<video>-Element im Wails-Frontend), wo Pausieren, Spulen und die
// tatsächliche Abspielgeschwindigkeit außerhalb dieses Pakets passieren.
//
// positions liefert die aktuelle Videoposition in Millisekundem, typischerweise
// alle 100-250ms vom Aufrufer geschickt (z.B. per JS-Event aus dem Video-Tag).
// Bei jedem Wert wird der Frame gesucht, der zu dieser Position gehört, und
// sofort ausgegeben - kein Timing-Loop, kein "warten bis Zielzeit erreicht",
// da die Zeitbasis extern (vom Video) kommt statt von uns.
//
// SyncIdleSentinel (oder jeder Wert < 0) setzt das Gerät sofort auf 0
// (Pause/Buffering), soft-startet bei der nächsten echten Position wieder.
//
// Extended-O funktioniert über TriggerExtendedO und skaliert nur die
// Amplitude (Vib/Sog × Faktor) - Video und Skript-Rhythmus laufen weiter.
func (p *Player) Sync(ctx context.Context, frames []funscript.Frame, positions <-chan int64) error {
	if len(frames) == 0 {
		return errNoFrames
	}
	logging.Info("player: Sync-Wiedergabe startet (externe Positionsquelle)", "frames", len(frames))
	defer func() {
		logging.Info("player: Sync-Wiedergabe beendet")
		p.setIntensityScale(1)
		p.Device.Stop() //nolint:errcheck // best effort beim Beenden
	}()

	firstFrame := true
	quiet := false // wegen fehlender Positionen auf 0 gesetzt
	stale := time.NewTimer(syncStaleAfter)
	defer stale.Stop()

	goQuiet := func(reason string) error {
		if firstFrame || quiet {
			return nil
		}
		logging.Info("player: "+reason+" - Gerät auf 0", "pos_ms", p.lastSyncPosMs)
		if err := p.setOutput(funscript.Frame{At: p.lastSyncPosMs}); err != nil {
			return err
		}
		if p.linearDevice() != nil {
			// A stroker halts where it is; the next position resends.
			if err := p.Device.Stop(); err != nil {
				return err
			}
			p.resetStroke()
			p.resetAxes()
		}
		quiet = true
		return nil
	}

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()

		case <-stale.C:
			if err := goQuiet("keine Videoposition mehr (Pause?)"); err != nil {
				return err
			}

		case opts, ok := <-p.extendedOCh:
			if !ok {
				continue
			}
			p.startExtendedO(ctx, opts)

		case posMs, ok := <-positions:
			if !ok {
				return nil // Kanal geschlossen = Wiedergabe im Frontend beendet
			}
			if posMs < 0 {
				// Idle sentinel: Pause/waiting — zero now; keep waiting for resume.
				stale.Stop()
				select {
				case <-stale.C:
				default:
				}
				stale.Reset(syncStaleAfter)
				if err := goQuiet("Videoposition idle (Pause/Buffering)"); err != nil {
					return err
				}
				continue
			}
			stale.Reset(syncStaleAfter)
			p.lastSyncPosMs = posMs
			f := frameAt(frames, posMs)
			// Nach einer Pause wie beim Start sanft hochfahren.
			if (firstFrame || quiet) && p.SoftStartMs > 0 {
				idle, err := p.softStartWithPositions(ctx, f, positions)
				if err != nil {
					return err
				}
				if idle {
					// Soft-start may already have written mid-ramp intensity.
					// goQuiet no-ops while firstFrame/quiet — always zero here.
					logging.Info("player: Videoposition idle during soft-start - Gerät auf 0", "pos_ms", p.lastSyncPosMs)
					if err := p.setOutput(funscript.Frame{At: p.lastSyncPosMs}); err != nil {
						return err
					}
					quiet = true
					continue
				}
			} else if err := p.setOutput(f); err != nil {
				return err
			}
			if err := p.driveStroke(posMs); err != nil {
				return err
			}
			if err := p.driveAxes(posMs); err != nil {
				return err
			}
			firstFrame, quiet = false, false
		}
	}
}

// setOutput schickt einen Frame skaliert ans Gerät (Extended-O senkt nur
// die Amplitude) und meldet den ausgegebenen Wert per OnFrame.
func (p *Player) setOutput(f funscript.Frame) error {
	scale := p.getIntensityScale()
	vibLevel, sucLevel := p.ChannelLevels()
	vib, suc := f.Vibration, f.Suction
	if p.PeakReduce() {
		vib, suc = ReducePeak(vib), ReducePeak(suc)
	}
	out := funscript.Frame{
		At:        f.At,
		Vibration: vib * scale * vibLevel,
		Suction:   suc * scale * sucLevel,
	}
	if err := p.Device.SetVibration(out.Vibration); err != nil {
		return err
	}
	if err := p.Device.SetSuction(out.Suction); err != nil {
		return err
	}
	if p.OnFrame != nil {
		p.OnFrame(out)
	}
	return nil
}

// softStart rampt in kleinen Schritten von 0 auf den Zielframe hoch, statt
// direkt zu springen - siehe Player.SoftStartMs. Genutzt beim allerersten
// Frame von Play() und Sync(). Berücksichtigt den aktuellen Amplitudenfaktor.
func (p *Player) softStart(ctx context.Context, target funscript.Frame) error {
	_, err := p.softStartWithPositions(ctx, target, nil)
	return err
}

// softStartWithPositions is softStart that also watches positions for an idle
// sentinel so Pause during the ramp zeros the device instead of finishing the
// ramp into a paused video.
func (p *Player) softStartWithPositions(ctx context.Context, target funscript.Frame, positions <-chan int64) (hitIdle bool, err error) {
	const steps = 10
	stepDur := time.Duration(p.SoftStartMs) * time.Millisecond / steps
	scale := p.getIntensityScale()
	for i := 1; i <= steps; i++ {
		frac := float64(i) / float64(steps)
		out := funscript.Frame{
			At:        target.At,
			Vibration: target.Vibration * frac * scale,
			Suction:   target.Suction * frac * scale,
		}
		if err := p.Device.SetVibration(out.Vibration); err != nil {
			return false, err
		}
		if err := p.Device.SetSuction(out.Suction); err != nil {
			return false, err
		}
		if p.OnFrame != nil {
			p.OnFrame(out)
		}
		if positions == nil {
			select {
			case <-time.After(stepDur):
			case <-ctx.Done():
				return false, ctx.Err()
			}
			continue
		}
		select {
		case <-time.After(stepDur):
		case <-ctx.Done():
			return false, ctx.Err()
		case posMs, ok := <-positions:
			if !ok {
				return false, nil
			}
			if posMs < 0 {
				return true, nil
			}
			// Later positive positions during ramp: keep ramping; Sync will
			// catch up on the next loop iteration.
		}
	}
	return false, nil
}

// frameAt sucht per Binärsuche den letzten Frame mit At <= posMs (bzw. den
// ersten, falls posMs davor liegt). Frames sind laut
// funscript.ToIntensityCurve() bereits zeitlich aufsteigend sortiert.
func frameAt(frames []funscript.Frame, posMs int64) funscript.Frame {
	i := sort.Search(len(frames), func(i int) bool { return frames[i].At > posMs })
	if i == 0 {
		return frames[0]
	}
	return frames[i-1]
}
