package sam

import (
	"math"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// FromFunscript wandelt ein geladenes .funscript in ein SAM Motion Script
// um - reine Übernahme von Zeit und Position, ohne die reicheren SAM-Felder
// zu raten: ein importiertes Funscript hat nur Position gesetzt (Type bleibt
// MotionUnknown), bis ein echter Producer
// (docs/SAM_ARCHITECTURE.md, "Deferred") sie befüllt.
//
// Profile/DeviceRecipe werden mit übernommen: ohne sie würde ein Tf/Tj-
// Skript nach einem SAM-Roundtrip stillschweigend die Abstand-statt-Hub-
// Zuordnung verlieren und mit der falschen Hardware-Zuordnung abgespielt
// (funscript.IsDistanceProfile prüft Metadata.Profile) - gefunden beim
// Testen mit echten tj-Skripten aus diesem Projekt, nicht nur ausgedacht.
func FromFunscript(fs *funscript.Script) *Script {
	// PlaybackActions honors top-level OFS inverted (100−pos) so SAM/device
	// play matches GetScriptCurve / ToIntensityCurve.
	actions := fs.PlaybackActions()
	frames := make([]Frame, len(actions))
	for i, a := range actions {
		frames[i] = Frame{
			Time:   a.At,
			Motion: Motion{Position: float64(a.Pos)},
		}
	}
	meta := Metadata{
		Creator: fs.Metadata.Creator,
		Source:  "funscript-import",
		Profile: fs.Metadata.Profile,
	}
	if dr := fs.Metadata.DeviceRecipe; dr != nil {
		meta.DeviceRecipe = &DeviceRecipe{
			Sync:                  dr.Sync,
			MinSuction:            dr.MinSuction,
			TickMs:                dr.TickMs,
			MaxSpeed:              dr.MaxSpeed,
			Smoothing:             dr.Smoothing,
			ContactVibration:      dr.ContactVibration,
			ContactVibrationSpan:  dr.ContactVibrationSpan,
			ContactVibrationCurve: dr.ContactVibrationCurve,
		}
	}
	if len(fs.Metadata.TrackingGaps) > 0 {
		meta.TrackingGaps = fromFunscriptGaps(fs.Metadata.TrackingGaps)
	}
	return &Script{
		Version:  ScriptVersion,
		Frames:   frames,
		Metadata: meta,
	}
}

// ToFunscript wandelt ein SAM Motion Script zurück in ein .funscript - nimmt
// Time/Position/Profile/DeviceRecipe, die reicheren SAM-Felder (Type,
// Energy, ...) gehen dabei verloren. Das ist der Zweck der
// Kompatibilitätsschicht: bestehende Player und Skripte funktionieren
// weiter, ohne SAM zu kennen (docs/SAM_ARCHITECTURE.md) - und jedes so
// erzeugte .funscript bleibt mit fungen_compare.py & Co. vergleichbar wie
// jedes andere.
func ToFunscript(s *Script) *funscript.Script {
	actions := make([]funscript.Action, len(s.Frames))
	for i, f := range s.Frames {
		actions[i] = funscript.Action{
			At: f.Time,
			// Runden statt Abschneiden: Position ist bei einem reinen
			// funscript-Import immer ganzzahlig (siehe FromFunscript), aber
			// ein künftiger Producer könnte echte Zwischenwerte schreiben -
			// int() würde die dann systematisch nach unten verzerren.
			Pos: int(math.Round(f.Motion.Position)),
		}
	}
	fs := &funscript.Script{Actions: actions}
	fs.Metadata.Creator = s.Metadata.Creator
	fs.Metadata.Profile = s.Metadata.Profile
	if dr := s.Metadata.DeviceRecipe; dr != nil {
		fs.Metadata.DeviceRecipe = &funscript.DeviceRecipe{
			Sync:                  dr.Sync,
			MinSuction:            dr.MinSuction,
			TickMs:                dr.TickMs,
			MaxSpeed:              dr.MaxSpeed,
			Smoothing:             dr.Smoothing,
			ContactVibration:      dr.ContactVibration,
			ContactVibrationSpan:  dr.ContactVibrationSpan,
			ContactVibrationCurve: dr.ContactVibrationCurve,
		}
	}
	if len(s.Metadata.TrackingGaps) > 0 {
		fs.Metadata.TrackingGaps = toFunscriptGaps(s.Metadata.TrackingGaps)
	}
	if len(actions) > 0 {
		fs.Metadata.Duration = funscript.MetaDuration(actions[len(actions)-1].At)
	}
	return fs
}
