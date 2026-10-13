package main

import "github.com/funfunpayer/SamNPlayer/funscript"

// ContactVibPreviewRequest is the Create Feel live probe (span / curve).
type ContactVibPreviewRequest = funscript.ContactVibPreviewRequest

// ContactVibPreviewResult is returned to the Create Feel panel.
type ContactVibPreviewResult = funscript.ContactVibPreviewResult

// PreviewContactVibration runs a fixed synthetic bounce through the contact-vib
// depth envelope so Feel Sensitivity / Curve show live feedback (MakeVibrationsExt-
// style). Probe only — not the user's clip; Everyday CSRT defaults unchanged.
func (a *App) PreviewContactVibration(req ContactVibPreviewRequest) ContactVibPreviewResult {
	return funscript.PreviewContactVibration(req)
}
