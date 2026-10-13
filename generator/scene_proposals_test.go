package generator

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeSceneJSON(t *testing.T, body string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "clip.scene.json")
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestLoadSceneProposals(t *testing.T) {
	// Same shape scene_roles.py writes (proposals only; windows ignored).
	p := writeSceneJSON(t, `{"version":1,"tool":"scene_roles","width":1280,"height":720,"windows":[],
	 "proposals":[
	  {"t_ms":14000,"start_ms":10000,"end_ms":18000,"scene_type":"titjob","confidence":0.55,
	   "primary":{"x":480,"y":320,"w":240,"h":240,"score":0.55,"class":"breasts"}},
	  {"t_ms":4000,"start_ms":0,"end_ms":8000,"scene_type":"blowjob","confidence":0.8,
	   "primary":{"x":560,"y":400,"w":160,"h":80,"score":0.8,"class":"mouth"},
	   "partner":{"x":560,"y":480,"w":80,"h":240,"score":0.8,"class":"penis"}},
	  {"t_ms":24000,"start_ms":20000,"end_ms":28000,"scene_type":null,"confidence":0.3,
	   "primary":{"x":1200,"y":0,"w":200,"h":80,"score":0.3,"class":"hand"}},
	  {"t_ms":34000,"start_ms":30000,"end_ms":38000,"scene_type":"handjob","confidence":0.6,
	   "primary":{"x":100,"y":100,"w":80,"h":80,"score":0.6,"class":"hand_right"},
	   "partner":{"x":0,"y":0,"w":0,"h":0,"score":0.6,"class":"penis"}}]}`)
	s, err := LoadSceneProposals(p)
	if err != nil {
		t.Fatal(err)
	}
	if s.Width != 1280 || s.Height != 720 || len(s.Proposals) != 3 {
		t.Fatalf("got %+v", s)
	}
	first := s.Proposals[0]
	if first.TMs != 4000 || first.SceneType != "blowjob" || first.Primary.Class != "mouth" ||
		first.Primary.Index != 1 || first.Partner == nil || first.Partner.Class != "penis" || first.Partner.Index != 2 {
		t.Fatalf("first = %+v partner %+v", first, first.Partner)
	}
	// Out-of-frame primary dropped; empty partner dropped but proposal kept.
	if s.Proposals[2].Primary.Class != "hand_right" || s.Proposals[2].Partner != nil {
		t.Fatalf("last = %+v", s.Proposals[2])
	}
	if p, ok := s.At(12000); !ok || p.SceneType != "titjob" {
		t.Fatalf("At(12000) = %+v %v", p, ok)
	}
	if p, ok := s.At(22000); !ok || p.SceneType != "titjob" { // gap: nearest centre (14 s beats 34 s)
		t.Fatalf("At(22000) = %+v %v", p, ok)
	}
	if _, ok := (SceneProposals{}).At(0); ok {
		t.Fatal("empty must be !ok")
	}
}

func TestLoadSceneProposalsErrors(t *testing.T) {
	for name, body := range map[string]string{
		"version": `{"version":2,"width":10,"height":10,"proposals":[]}`,
		"size":    `{"version":1,"proposals":[]}`,
		"json":    `nope`,
	} {
		if _, err := LoadSceneProposals(writeSceneJSON(t, body)); err == nil {
			t.Errorf("%s: want error", name)
		}
	}
	if _, err := LoadSceneProposals(filepath.Join(t.TempDir(), "missing.json")); err == nil {
		t.Error("missing file: want error")
	}
}

func TestApplySceneProposal(t *testing.T) {
	p := SceneProposal{TMs: 4000, StartMs: 0, EndMs: 8000, SceneType: "titjob", Confidence: 0.7,
		Primary: ROICandidate{X: 10, Y: 20, W: 30, H: 40, Class: "breasts"},
		Partner: &ROICandidate{X: 50, Y: 60, W: 70, H: 80, Class: "penis"}}
	lines := func(l []string) string { return strings.Join(l, "\n") }

	// Setting off: ROI + class filled, partner only reported.
	var roi ROI
	o := Options{}
	got := ApplySceneProposal(&roi, &o, p, false)
	if roi != (ROI{10, 20, 30, 40}) || o.RegionClass != "breasts" || o.ROI2.W != 0 ||
		!strings.Contains(lines(got), "proposal only (Apply AI setup automatically is off)") {
		t.Fatalf("off: roi=%+v o=%+v\n%s", roi, o, lines(got))
	}
	// Setting on: ROI2 tracked + class 2.
	roi, o = ROI{}, Options{ROI2Fixed: true}
	got = ApplySceneProposal(&roi, &o, p, true)
	if o.ROI2 != (ROI{50, 60, 70, 80}) || o.ROI2Fixed || o.RegionClass2 != "penis" ||
		!strings.Contains(lines(got), "applied ROI2 50,60,70,80 = penis, tracked") {
		t.Fatalf("on: o=%+v\n%s", o, lines(got))
	}
	// User values always win.
	roi, o = ROI{1, 2, 3, 4}, Options{RegionClass: "mouth", ROI2: ROI{5, 6, 7, 8}}
	got = ApplySceneProposal(&roi, &o, p, true)
	if roi != (ROI{1, 2, 3, 4}) || o.RegionClass != "mouth" || o.ROI2 != (ROI{5, 6, 7, 8}) ||
		!strings.Contains(lines(got), "ROI2 already set by the user") {
		t.Fatalf("user wins: roi=%+v o=%+v\n%s", roi, o, lines(got))
	}
	// Distance profile: ROI2 would change the curve source - never applied.
	roi, o = ROI{}, Options{Profile: "tj"}
	got = ApplySceneProposal(&roi, &o, p, true)
	if o.ROI2.W != 0 || !strings.Contains(lines(got), "two-point tracking") {
		t.Fatalf("distance: o=%+v\n%s", o, lines(got))
	}
	// Aliases (hand_right from scene_roles / YOLO) → canonical RegionClass.
	alias := SceneProposal{TMs: 34000, StartMs: 30000, EndMs: 38000, SceneType: "handjob", Confidence: 0.6,
		Primary: ROICandidate{X: 100, Y: 100, W: 80, H: 80, Class: "hand_right"},
		Partner: &ROICandidate{X: 200, Y: 200, W: 40, H: 60, Class: "glans"}}
	roi, o = ROI{}, Options{}
	got = ApplySceneProposal(&roi, &o, alias, true)
	if o.RegionClass != "hand_2" || o.RegionClass2 != "glans" ||
		!strings.Contains(lines(got), "applied region class hand_2") {
		t.Fatalf("alias normalize: o=%+v\n%s", o, lines(got))
	}
}
