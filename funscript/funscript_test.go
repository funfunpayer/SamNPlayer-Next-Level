package funscript

import "testing"

// Das Urteil des Quality Doctor muss als eigenes Feld durchgereicht werden.
// Es aus QualityScore >= 0.5 abzuleiten wäre falsch: der Quality Doctor kennt
// zusätzlich harte Ausschlusskriterien (z.B. >40% der Videolänge ohne
// Tracking-Daten), die trotz ausreichendem Punktestand zum Scheitern führen.
func TestParseQualityMetadata(t *testing.T) {
	tests := []struct {
		name       string
		json       string
		wantScore  *float64
		wantPassed *bool
	}{
		{
			name:       "nicht bestanden trotz Score über 0.5",
			json:       `{"actions":[{"at":0,"pos":0},{"at":100,"pos":90}],"metadata":{"quality_score":0.63,"quality_passed":false}}`,
			wantScore:  ptr(0.63),
			wantPassed: ptr(false),
		},
		{
			name:       "bestanden",
			json:       `{"actions":[{"at":0,"pos":0},{"at":100,"pos":90}],"metadata":{"quality_score":1.0,"quality_passed":true}}`,
			wantScore:  ptr(1.0),
			wantPassed: ptr(true),
		},
		{
			name:       "Altskript ohne quality_passed",
			json:       `{"actions":[{"at":0,"pos":0},{"at":100,"pos":90}],"metadata":{"quality_score":0.8}}`,
			wantScore:  ptr(0.8),
			wantPassed: nil,
		},
		{
			name:       "Skript ganz ohne Quality-Metadaten",
			json:       `{"actions":[{"at":0,"pos":0},{"at":100,"pos":90}]}`,
			wantScore:  nil,
			wantPassed: nil,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			s, err := Parse([]byte(tc.json))
			if err != nil {
				t.Fatalf("Parse: %v", err)
			}
			checkPtr(t, "QualityScore", s.Metadata.QualityScore, tc.wantScore)
			checkPtr(t, "QualityPassed", s.Metadata.QualityPassed, tc.wantPassed)
		})
	}
}

func ptr[T any](v T) *T { return &v }

func checkPtr[T comparable](t *testing.T, field string, got, want *T) {
	t.Helper()
	switch {
	case want == nil && got != nil:
		t.Errorf("%s = %v, erwartet nil", field, *got)
	case want != nil && got == nil:
		t.Errorf("%s = nil, erwartet %v", field, *want)
	case want != nil && got != nil && *got != *want:
		t.Errorf("%s = %v, erwartet %v", field, *got, *want)
	}
}
