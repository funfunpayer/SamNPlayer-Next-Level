package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/funfunpayer/SamNPlayer/generator"
)

func TestApplySceneProposalsCLI(t *testing.T) {
	p := filepath.Join(t.TempDir(), "clip.scene.json")
	body := `{"version":1,"width":1280,"height":720,"proposals":[
	 {"t_ms":4000,"start_ms":0,"end_ms":8000,"scene_type":"blowjob","confidence":0.8,
	  "primary":{"x":560,"y":400,"w":160,"h":80,"score":0.8,"class":"mouth"},
	  "partner":{"x":560,"y":480,"w":80,"h":240,"score":0.8,"class":"penis"}},
	 {"t_ms":14000,"start_ms":10000,"end_ms":18000,"scene_type":"penetration","confidence":0.6,
	  "primary":{"x":100,"y":100,"w":80,"h":80,"score":0.6,"class":"buttocks"}}]}`
	if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	var log []string
	var roi generator.ROI
	opts := generator.Options{}
	if err := applySceneProposals(p, 0, false, &roi, &opts, func(l string) { log = append(log, l) }); err != nil {
		t.Fatal(err)
	}
	if roi.X != 560 || roi.Y != 400 || roi.W != 160 || roi.H != 80 || opts.RegionClass != "mouth" {
		t.Fatalf("roi=%+v class=%q", roi, opts.RegionClass)
	}
	if opts.ROI2.W != 0 || !strings.Contains(strings.Join(log, "\n"), "proposal only") {
		t.Fatalf("ROI2 set without --scene-apply: %+v log=%v", opts.ROI2, log)
	}
	// --scene-apply: the partner becomes ROI2 (tracked), logged.
	roi, opts, log = generator.ROI{}, generator.Options{}, nil
	if err := applySceneProposals(p, 0, true, &roi, &opts, func(l string) { log = append(log, l) }); err != nil {
		t.Fatal(err)
	}
	if opts.ROI2.Y != 480 || opts.ROI2.H != 240 || opts.RegionClass2 != "penis" || opts.ROI2Fixed {
		t.Fatalf("ROI2=%+v class2=%q", opts.ROI2, opts.RegionClass2)
	}
	if !strings.Contains(strings.Join(log, "\n"), "applied ROI2 560,480,80,240") {
		t.Fatalf("log=%v", log)
	}
	// A user --roi is kept; non-canonical part: no region class.
	roi, opts = generator.ROI{X: 1, Y: 2, W: 3, H: 4}, generator.Options{}
	if err := applySceneProposals(p, 12000, true, &roi, &opts, func(string) {}); err != nil {
		t.Fatal(err)
	}
	if roi.X != 1 || opts.RegionClass != "" {
		t.Fatalf("roi=%+v class=%q", roi, opts.RegionClass)
	}
	if err := applySceneProposals(p+".missing", 0, true, &roi, &opts, func(string) {}); err == nil {
		t.Fatal("missing file: want error")
	}
}

func TestParseROI(t *testing.T) {
	if r, err := parseROI("1, 2,3,4"); err != nil || r.X != 1 || r.H != 4 {
		t.Fatalf("%+v %v", r, err)
	}
	for _, bad := range []string{"1,2,3", "a,2,3,4"} {
		if _, err := parseROI(bad); err == nil {
			t.Errorf("%q: want error", bad)
		}
	}
}
