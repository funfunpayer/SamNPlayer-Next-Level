package generator

import (
	"strings"
	"testing"
)

// Die neuen Optionen müssen tatsächlich als CLI-Argumente beim Python-Skript
// ankommen - eine GUI-Checkbox, die nirgends landet, ist schlimmer als keine.
func TestGeneratorOptionsReachPython(t *testing.T) {
	cases := []struct {
		name string
		opts Options
		want []string
	}{
		{"Standard ohne Extras", Options{}, nil},
		{"Backend csrt explizit (Standard, kein Flag nötig)", Options{Backend: "csrt"}, nil},
		{"Auto-Retry", Options{AutoRetry: true}, []string{"--auto-retry"}},
		{"waagerechte Achse erzwingen", Options{Axis: "x"}, []string{"--axis", "x"}},
		{"senkrechte Achse erzwingen", Options{Axis: "y"}, []string{"--axis", "y"}},
		{"Geschwindigkeitsgrenze", Options{MaxSpeed: 400}, []string{"--max-speed", "400"}},
		{"adaptive Keyframes", Options{AdaptiveKeyframeError: 6}, []string{"--adaptive-keyframes", "6"}},
		{"Region pro Szene", Options{PerSceneROI: true}, []string{"--per-scene-roi"}},
		{"Profil weich", Options{Profile: "weich"}, []string{"--profile", "weich"}},
		{"Profil autotune", Options{Profile: "autotune"}, []string{"--profile", "autotune"}},
		{"Profil tj", Options{Profile: "tj"}, []string{"--profile", "tj"}},
		{"Profil tf", Options{Profile: "tf"}, []string{"--profile", "tf"}},
		{"ROI2", Options{ROI2: ROI{X: 10, Y: 20, W: 30, H: 40}},
			[]string{"--roi2", "10,20,30,40"}},
		{"ROI2 fixed", Options{ROI2: ROI{X: 10, Y: 20, W: 30, H: 40}, ROI2Fixed: true},
			[]string{"--roi2", "10,20,30,40", "--roi2-fixed"}},
		{"tj + ROI2", Options{Profile: "tj", ROI2: ROI{X: 1, Y: 2, W: 3, H: 4}},
			[]string{"--profile", "tj", "--roi2", "1,2,3,4"}},
		{"region classes", Options{RegionClass: "glans", RegionClass2: "nipples"},
			[]string{"--region-class", "glans", "--region-class2", "nipples"}},
		{"extra target + soft mask", Options{
			ExtraTargets: []NamedROI{{X: 5, Y: 6, W: 7, H: 8}},
			MaskROIs:     []ROI{{X: 1, Y: 2, W: 3, H: 4}},
		}, []string{"--target", "5,6,7,8", "--mask", "1,2,3,4"}},
		{"extra target with class", Options{
			ExtraTargets: []NamedROI{{X: 5, Y: 6, W: 7, H: 8, Class: "mouth"}},
		}, []string{"--target", "5,6,7,8,mouth"}},
		{"Kontakt-Vibration", Options{Profile: "tj", ContactVibration: true},
			[]string{"--contact-vibration"}},
		{"Kontakt-Vibration Normal/standard", Options{Profile: "standard", ContactVibration: true},
			[]string{"--contact-vibration"}},
		{"Kontakt-Vibration Span+Kurve", Options{Profile: "tj", ContactVibration: true,
			ContactVibrationSpan: 0.55, ContactVibrationCurve: "soft"},
			[]string{"--contact-vibration", "--contact-vibration-span", "0.55",
				"--contact-vibration-curve", "soft"}},
		{"Kontakt-Vibration impulse curve", Options{Profile: "standard", ContactVibration: true,
			ContactVibrationCurve: "impulse"},
			[]string{"--contact-vibration", "--contact-vibration-curve", "impulse"}},
		{"Backend flow", Options{Backend: "flow"}, []string{"--backend", "flow"}},
		{"Backend grid_lk", Options{Backend: "grid_lk"}, []string{"--backend", "grid_lk"}},
		{"Audio-Tempo-Prüfung", Options{AudioCheck: true}, []string{"--audio-check"}},
		{"Seek StartTimeSec", Options{StartTimeSec: 3.5}, []string{"--start-seconds", "3.500"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			args := BuildArgsForTest(tc.opts)
			joined := strings.Join(args, " ")
			for _, w := range tc.want {
				if !strings.Contains(joined, w) {
					t.Errorf("Argument %q fehlt in: %s", w, joined)
				}
			}
			if tc.want == nil {
				for _, flag := range []string{"--auto-retry", "--axis", "--max-speed",
					"--adaptive-keyframes", "--per-scene-roi", "--profile", "--roi2",
					"--contact-vibration", "--contact-vibration-span",
					"--contact-vibration-curve", "--backend", "--audio-check",
					"--start-seconds"} {
					if strings.Contains(joined, flag) {
						t.Errorf("unerwartetes Argument %q bei Standardoptionen: %s", flag, joined)
					}
				}
			}
		})
	}
}
