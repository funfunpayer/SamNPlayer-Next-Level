package funscript

import "testing"

func TestPreviewContactVibrationSpanEarlier(t *testing.T) {
	late := PreviewContactVibration(ContactVibPreviewRequest{Span: 0.85, Curve: ContactCurveLinear})
	early := PreviewContactVibration(ContactVibPreviewRequest{Span: 0.45, Curve: ContactCurveLinear})
	if late.ActivePct >= early.ActivePct {
		t.Fatalf("earlier span should be more active: late=%.1f early=%.1f", late.ActivePct, early.ActivePct)
	}
	if len(late.Sample) < 4 || len(early.Sample) < 4 {
		t.Fatalf("expected sample polylines")
	}
}

func TestPreviewContactVibrationImpulseQuieter(t *testing.T) {
	soft := PreviewContactVibration(ContactVibPreviewRequest{Span: 0.75, Curve: ContactCurveSoft})
	impulse := PreviewContactVibration(ContactVibPreviewRequest{Span: 0.75, Curve: ContactCurveImpulse})
	if impulse.ActivePct >= soft.ActivePct {
		t.Fatalf("impulse should be quieter than soft: impulse=%.1f soft=%.1f", impulse.ActivePct, soft.ActivePct)
	}
	if soft.PeakVib <= 0 || impulse.Hint == "" {
		t.Fatalf("soft peak=%.3f impulse hint=%q", soft.PeakVib, impulse.Hint)
	}
}
