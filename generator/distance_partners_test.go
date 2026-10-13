package generator

import "testing"

func TestDistancePartnersActive(t *testing.T) {
	roi2 := ROI{X: 1, Y: 2, W: 30, H: 40}
	cases := []struct {
		name    string
		profile string
		roi2    ROI
		want    bool
	}{
		{"tf with roi2", "tf", roi2, true},
		{"tj with roi2", "tj", roi2, true},
		{"autotune ignores roi2", "autotune", roi2, false},
		{"standard ignores roi2", "standard", roi2, false},
		{"weich ignores roi2", "weich", roi2, false},
		{"tf without roi2", "tf", ROI{}, false},
		{"empty profile ignores roi2", "", roi2, false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := distancePartnersActive(Options{Profile: tc.profile, ROI2: tc.roi2})
			if got != tc.want {
				t.Fatalf("got %v want %v", got, tc.want)
			}
		})
	}
}
