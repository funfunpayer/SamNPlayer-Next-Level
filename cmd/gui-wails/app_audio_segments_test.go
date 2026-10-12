package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestLoadFunscriptExposesAudioCheckSegments(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "clip.funscript")
	body := []byte(`{"actions":[{"at":0,"pos":10},{"at":2000,"pos":90}],"metadata":{}}`)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	hz := 1.25
	if err := funscript.StampAudioCheck(path, &funscript.AudioCheck{
		SpeechHoldMs: 1500,
		AudioHz:      &hz,
		Segments: []funscript.AudioSegmentHint{
			{Label: "holding", StartMs: 0, EndMs: 1500, SpeechHold: true},
			{Label: "gentle", StartMs: 1500, EndMs: 2000},
		},
	}); err != nil {
		t.Fatal(err)
	}

	a := &App{}
	info, err := a.LoadFunscript(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.SpeechHoldMs != 1500 {
		t.Fatalf("speechHoldMs=%d", info.SpeechHoldMs)
	}
	if info.AudioHz < 1.24 || info.AudioHz > 1.26 {
		t.Fatalf("audioHz=%v want ~1.25", info.AudioHz)
	}
	if len(info.AudioSegments) != 2 {
		t.Fatalf("segments=%d", len(info.AudioSegments))
	}
	if info.AudioSegments[0].Label != "holding" || !info.AudioSegments[0].SpeechHold {
		t.Fatalf("seg0=%+v", info.AudioSegments[0])
	}
	if info.AudioSegments[1].Label != "gentle" || info.AudioSegments[1].StartMs != 1500 {
		t.Fatalf("seg1=%+v", info.AudioSegments[1])
	}
}

func TestFillScriptInfoAudioCheckFromCompanion(t *testing.T) {
	dir := t.TempDir()
	funPath := filepath.Join(dir, "clip.funscript")
	samnPath := filepath.Join(dir, "clip.samn")
	body := []byte(`{"actions":[{"at":0,"pos":10},{"at":1000,"pos":90}],"metadata":{}}`)
	if err := os.WriteFile(funPath, body, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := funscript.StampAudioCheck(funPath, &funscript.AudioCheck{
		SpeechHoldMs: 800,
		Segments: []funscript.AudioSegmentHint{
			{Label: "intense", StartMs: 100, EndMs: 900},
		},
	}); err != nil {
		t.Fatal(err)
	}
	// .samn path present; script payload has no audio_check (ToFunscript gap).
	if err := os.WriteFile(samnPath, []byte(`{"format":"samn"}`), 0o644); err != nil {
		t.Fatal(err)
	}

	info := &ScriptInfo{}
	fillScriptInfoAudioCheck(info, &funscript.Script{}, samnPath)
	if info.SpeechHoldMs != 800 {
		t.Fatalf("speechHoldMs=%d", info.SpeechHoldMs)
	}
	if len(info.AudioSegments) != 1 || info.AudioSegments[0].Label != "intense" {
		t.Fatalf("segments=%+v", info.AudioSegments)
	}
}

func TestFillScriptInfoAudioCheckNilSafe(t *testing.T) {
	fillScriptInfoAudioCheck(nil, nil, "")
	info := &ScriptInfo{}
	fillScriptInfoAudioCheck(info, nil, "/tmp/missing.funscript")
	if info.SpeechHoldMs != 0 || len(info.AudioSegments) != 0 {
		t.Fatalf("expected empty, got %+v", info)
	}
}
