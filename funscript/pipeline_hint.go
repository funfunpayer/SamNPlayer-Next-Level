package funscript

// PipelineSuggestion is an automatic preselection for generate options.
type PipelineSuggestion struct {
	Backend string `json:"Backend"`
	Profile string `json:"Profile"`
	Reason  string `json:"Reason"`
	GoPath  bool   `json:"GoPath"`
}

// SuggestPipeline picks backend + profile from marked ROI geometry.
// Always CSRT — the product tracker. Go CSRT when OpenCV is linked;
// otherwise Generate uses Python CSRT as the product path until Windows
// in-binary CSRT ships (#120). Research backends remain CLI --backend only.
func SuggestPipeline(roiW, roiH, roi2W, roi2H int) PipelineSuggestion {
	if roi2W > 0 && roi2H > 0 && roiW > 0 && roiH > 0 {
		// Contact-first: two marks do not force a Tf/Tj profile. Stroke +
		// Contact vib is the product path; distance Tf/Tj stays CLI.
		return PipelineSuggestion{
			Backend: "csrt",
			Profile: "standard",
			Reason:  "Everyday: CSRT tip + Stroke (Contact vib optional) — measured best vs FunGen",
			GoPath:  true,
		}
	}
	if roiW <= 0 || roiH <= 0 {
		return PipelineSuggestion{
			Backend: "csrt",
			Profile: "standard",
			Reason:  "Everyday: auto-find tip → CSRT Stroke (4-zone is advanced / weaker)",
			GoPath:  true,
		}
	}
	_ = SuggestBackend(roiW, roiH)
	return PipelineSuggestion{
		Backend: "csrt",
		Profile: "standard",
		Reason:  "CSRT tip — first choice on clip_ausschnitt (windowed r≈0.59 hub)",
		GoPath:  true,
	}
}
