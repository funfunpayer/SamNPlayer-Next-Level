package sam

import (
	"github.com/funfunpayer/SamNPlayer/funscript"
)

// ToDeviceFrames baut Geräte-Frames aus einem (typischerweise enriched)
// SAM-Skript. Sog folgt Position/Range; Kontakt-Vibration kommt aus
// Motion.Intensity (nicht erneut aus Pos geraten). TrackingGaps
// (Metadata ∪ MapOptions) und Confidence≈0 (nach Enrich) halten Vib bei 0.
//
// Das ist der erste Playback-Consumer des SAM-Modells: .funscript bleibt
// die Datei, SAM die Bewegungsbeschreibung dazwischen.
func ToDeviceFrames(s *Script, opts funscript.MapOptions) []funscript.Frame {
	if s == nil || len(s.Frames) < 2 {
		return nil
	}
	if opts.TickMs <= 0 {
		opts.TickMs = 50
	}
	duration := s.Duration()
	frames := make([]funscript.Frame, 0, duration/opts.TickMs+1)
	segIdx := 0
	var prevVib, prevSuc float64
	var prevContactVib float64
	envelope := opts.ContactVibrationEnvelope
	envelopeOn := true
	if envelope < 0 {
		envelopeOn = false
	} else if envelope == 0 || envelope >= 1 {
		envelope = funscript.DefaultContactEnvelopeSmooth
	}
	contactOn := opts.ContactVibration && opts.Sync == funscript.SyncSuctionPosition
	// Metadata-Gaps (aus Enrich/Sidecar) ∪ opts.TrackingGaps (GUI/Classic-Pfad).
	gaps := append(toFunscriptGaps(s.Metadata.TrackingGaps), opts.TrackingGaps...)
	confidenceLive := hasPositiveConfidence(s)
	// Kontakt-Schwelle aus denselben Frames+Span wie Enrich — verhindert, dass
	// Intensity-Interpolation unter der Schwelle schon vibriert (Classic wertet
	// Pos pro Tick; reines Intensity-Lerp würde darunter „vorvibrieren“).
	contactMin, contactThreshOn := contactThresholdFromScript(s, opts)

	inGap := func(t int64) bool {
		for _, g := range gaps {
			if t >= g.StartMs && t <= g.EndMs {
				return true
			}
		}
		return false
	}

	for t := int64(0); t <= duration; t += opts.TickMs {
		for segIdx < len(s.Frames)-2 && s.Frames[segIdx+1].Time <= t {
			segIdx++
		}
		a, b := s.Frames[segIdx], s.Frames[segIdx+1]
		dt := b.Time - a.Time
		if dt <= 0 {
			dt = 1
		}
		frac := float64(t-a.Time) / float64(dt)
		pos := a.Motion.Position + frac*(b.Motion.Position-a.Motion.Position)
		posSignal := clamp01(pos / 100.0)
		intens := a.Motion.Intensity + frac*(b.Motion.Intensity-a.Motion.Intensity)
		conf := a.Motion.Confidence + frac*(b.Motion.Confidence-a.Motion.Confidence)

		var vib, suc float64
		mutedForTick := false
		switch opts.Sync {
		case funscript.SyncSuctionPosition:
			suc = posSignal
			if a.Motion.Range > 0 || b.Motion.Range > 0 {
				ra, rb := a.Motion.Range, b.Motion.Range
				if ra == 0 {
					ra = clamp01(a.Motion.Position / 100)
				}
				if rb == 0 {
					rb = clamp01(b.Motion.Position / 100)
				}
				suc = ra + frac*(rb-ra)
			}
			muted := inGap(t) || (confidenceLive && conf < 0.5)
			if contactThreshOn && pos < contactMin {
				muted = true
			}
			mutedForTick = muted
			if contactOn && !muted {
				vib = clamp01(intens)
				if vib > 0 && opts.MinVibration > 0 {
					vib = opts.MinVibration + vib*(1-opts.MinVibration)
				}
			}
			// Envelope nur außerhalb Gaps; im Gap hart auf 0 (kein Ausklingen
			// von prevContactVib in Tracker-Verlustfenster).
			if contactOn && envelopeOn && !muted {
				if len(frames) > 0 {
					vib = envelope*prevContactVib + (1-envelope)*vib
				}
				prevContactVib = vib
			} else if muted {
				vib = 0
				prevContactVib = 0
			}
		default:
			// SAM-Consumer v1: nur Tf/Tj/Contact-Pfad; sonst Fallback leer.
			return nil
		}
		if opts.Smoothing > 0 && len(frames) > 0 {
			vib = opts.Smoothing*prevVib + (1-opts.Smoothing)*vib
			suc = opts.Smoothing*prevSuc + (1-opts.Smoothing)*suc
		}
		// Gap/Confidence/Schwelle gewinnen über Recipe-Smoothing.
		if mutedForTick {
			vib = 0
		}
		prevVib, prevSuc = vib, suc
		frames = append(frames, funscript.Frame{At: t, Vibration: vib, Suction: suc})
	}
	return frames
}

// contactThresholdFromScript: gleiche Span-Logik wie Enrich/ToIntensityCurve.
func contactThresholdFromScript(s *Script, opts funscript.MapOptions) (contactMin float64, ok bool) {
	if s == nil || len(s.Frames) == 0 {
		return 0, false
	}
	span := opts.ContactVibrationSpan
	if span <= 0 && s.Metadata.DeviceRecipe != nil {
		span = s.Metadata.DeviceRecipe.ContactVibrationSpan
	}
	span = funscript.EffectiveContactSpan(span)
	posMin := s.Frames[0].Motion.Position
	posMax := posMin
	for _, f := range s.Frames[1:] {
		p := f.Motion.Position
		if p < posMin {
			posMin = p
		}
		if p > posMax {
			posMax = p
		}
	}
	if posMax-posMin < 5.0 {
		return 0, false
	}
	return posMin + span*(posMax-posMin), true
}

// HasContactIntensity meldet, ob das SAM-Skript brauchbare Kontakt-Intensity
// trägt. Dünne Imports (nur Position) liefern false — GUI darf sie nicht
// einem angereicherten Enrich-Pfad vorziehen.
func HasContactIntensity(s *Script) bool {
	if s == nil {
		return false
	}
	for _, f := range s.Frames {
		if f.Motion.Intensity > 0.001 {
			return true
		}
	}
	return false
}

func hasPositiveConfidence(s *Script) bool {
	for _, f := range s.Frames {
		if f.Motion.Confidence > 0 {
			return true
		}
	}
	return false
}

// PlaybackFramesFromFunscript: Enrich in-memory, dann ToDeviceFrames —
// .funscript-Datei unverändert, SAM dazwischen.
func PlaybackFramesFromFunscript(fs *funscript.Script, opts funscript.MapOptions) []funscript.Frame {
	if fs == nil {
		return nil
	}
	if !(opts.ContactVibration && opts.Sync == funscript.SyncSuctionPosition) {
		return fs.ToIntensityCurve(opts)
	}
	// opts.TrackingGaps in Metadata spiegeln, damit Enrich Intensity/Confidence
	// in denselben Fenstern nullt (ToDeviceFrames merged zusätzlich).
	if len(opts.TrackingGaps) > 0 && len(fs.Metadata.TrackingGaps) == 0 {
		fs = cloneScriptMetaGaps(fs, opts.TrackingGaps)
	} else if len(opts.TrackingGaps) > 0 {
		fs = cloneScriptMetaGaps(fs, mergeFunscriptGaps(fs.Metadata.TrackingGaps, opts.TrackingGaps))
	}
	enriched := FromFunscriptEnriched(fs)
	// Tick-dichte Intensity aus Pos (Classic-Parity), dann Geräte-Mapping.
	dense := Densify(enriched, opts.TickMs, opts)
	out := ToDeviceFrames(dense, opts)
	if out == nil {
		return fs.ToIntensityCurve(opts)
	}
	return out
}

func mergeFunscriptGaps(a, b []funscript.TrackingGap) []funscript.TrackingGap {
	if len(a) == 0 {
		return append([]funscript.TrackingGap(nil), b...)
	}
	if len(b) == 0 {
		return append([]funscript.TrackingGap(nil), a...)
	}
	out := make([]funscript.TrackingGap, 0, len(a)+len(b))
	out = append(out, a...)
	out = append(out, b...)
	return out
}

// cloneScriptMetaGaps: flache Kopie nur für Gap-Override (Actions shared).
func cloneScriptMetaGaps(fs *funscript.Script, gaps []funscript.TrackingGap) *funscript.Script {
	cp := *fs
	cp.Metadata = fs.Metadata
	cp.Metadata.TrackingGaps = append([]funscript.TrackingGap(nil), gaps...)
	return &cp
}
