package player

import (
	"context"
	"time"
)

// ExtendedOOptions steuert den Extended-O-Klimax-Modus: die Skript-Kurve
// läuft unverändert weiter (Rhythmus/Form bleiben), aber Vibration und Sog
// werden mit MinLevel multipliziert - die Kurve wird nur niedriger, nicht
// flach eingefroren. Semantik weicht bewusst vom früheren "auf MinLevel
// halten und Timeline pausieren" ab; am Gerät fühlt es sich wie "alles
// etwas schwächer" an, nicht wie ein Stopp.
type ExtendedOOptions struct {
	// MinLevel ist der Amplitudenfaktor (0-1), mit dem Vib/Sog während
	// des Haltens multipliziert werden. 0.1 = 10% der Kurvenhöhe.
	MinLevel float64
	// HoldDuration ist die Zeit, die auf dem reduzierten Faktor gehalten wird.
	HoldDuration time.Duration
	// RestoreDuration ist die Rampzeit von 1.0 → MinLevel und zurück
	// MinLevel → 1.0. 0 = sofort umschalten (kein Ramping).
	RestoreDuration time.Duration
}

// DefaultExtendedOOptions liefert plausible Startwerte.
func DefaultExtendedOOptions() ExtendedOOptions {
	return ExtendedOOptions{
		MinLevel:        0.1,
		HoldDuration:    10 * time.Second,
		RestoreDuration: 500 * time.Millisecond,
	}
}

// TriggerExtendedO fordert einen Extended-O-Zyklus an. Nicht-blockierend:
// die laufende Wiedergabe (Play/Sync) startet beim nächsten Tick den
// Skalierungszyklus im Hintergrund. Ist bereits ein Trigger anhängig oder
// ein Zyklus aktiv, wird der neue verworfen, statt sich aufzustauen.
func (p *Player) TriggerExtendedO(opts ExtendedOOptions) {
	select {
	case p.extendedOCh <- opts:
	default:
		// Es wartet schon ein Trigger - zusätzliche Tastendrücke ignorieren.
	}
}

func (p *Player) getIntensityScale() float64 {
	p.intensityMu.Lock()
	defer p.intensityMu.Unlock()
	return 1 - p.intensityCut
}

func (p *Player) setIntensityScale(s float64) {
	if s < 0 {
		s = 0
	}
	if s > 1 {
		s = 1
	}
	p.intensityMu.Lock()
	p.intensityCut = 1 - s
	p.intensityMu.Unlock()
}

// drainExtendedO holt einen anhängigen Trigger ab und startet den
// Amplituden-Zyklus. Blockiert die Skript-Timeline nicht.
func (p *Player) drainExtendedO(ctx context.Context) {
	select {
	case opts := <-p.extendedOCh:
		p.startExtendedO(ctx, opts)
	default:
	}
}

func (p *Player) startExtendedO(ctx context.Context, opts ExtendedOOptions) {
	if opts.MinLevel < 0 {
		opts.MinLevel = 0
	}
	if opts.MinLevel > 1 {
		opts.MinLevel = 1
	}
	if !p.eoBusy.CompareAndSwap(false, true) {
		p.logf("[extended-o] bereits aktiv, neuer Trigger ignoriert")
		return
	}
	go func() {
		defer p.eoBusy.Store(false)
		defer p.setIntensityScale(1)

		from := p.getIntensityScale()
		target := opts.MinLevel
		p.logf("[extended-o] Amplitude auf %.0f%% für %s (Rhythmus unverändert)...",
			target*100, opts.HoldDuration)

		if err := p.rampScale(ctx, from, target, opts.RestoreDuration); err != nil {
			return
		}
		select {
		case <-time.After(opts.HoldDuration):
		case <-ctx.Done():
			return
		}
		if err := p.rampScale(ctx, target, 1, opts.RestoreDuration); err != nil {
			return
		}
		p.logf("[extended-o] Amplitude wiederhergestellt")
	}()
}

func (p *Player) rampScale(ctx context.Context, from, to float64, dur time.Duration) error {
	if dur <= 0 {
		p.setIntensityScale(to)
		return nil
	}
	const steps = 10
	stepDur := dur / steps
	for i := 1; i <= steps; i++ {
		frac := float64(i) / float64(steps)
		p.setIntensityScale(from + frac*(to-from))
		select {
		case <-time.After(stepDur):
		case <-ctx.Done():
			return ctx.Err()
		}
	}
	return nil
}
