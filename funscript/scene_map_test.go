package funscript

import (
	"encoding/json"
	"testing"
)

func TestEncodeDecodeSceneMapScore(t *testing.T) {
	in := []uint8{0, 1, 127, 255, 42}
	b64 := EncodeSceneMapScore(in)
	if b64 == "" {
		t.Fatal("expected non-empty base64")
	}
	out, err := DecodeSceneMapScore(b64)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != len(in) {
		t.Fatalf("len %d != %d", len(out), len(in))
	}
	for i := range in {
		if out[i] != in[i] {
			t.Fatalf("byte %d: got %d want %d", i, out[i], in[i])
		}
	}
	if EncodeSceneMapScore(nil) != "" {
		t.Fatal("nil score should encode empty")
	}
}

func TestSceneMapDataJSONRoundTrip(t *testing.T) {
	reviewed := false
	at := int64(96000)
	m := &SceneMapData{
		Version: 1,
		Video: SceneMapVideo{
			DurationMs: 280125,
			Width:      1280,
			Height:     720,
			Sha256Head: "abc",
		},
		Grid: SceneMapGrid{Cols: 16, Rows: 9},
		Windows: []SceneMapWindow{{
			StartMs:  0,
			EndMs:    8000,
			TempoHz:  1.0,
			ScoreB64: EncodeSceneMapScore([]uint8{10, 20, 30}),
			Chosen:   88,
			SignRule: "tracker",
			TrackerR: 0.41,
			Box:      []float64{650, 540},
			Marks:    []string{"m1"},
		}},
		Marks: []SceneMapMark{
			{
				ID: "m1", Kind: "exclude", Rect: []int{60, 560, 260, 160},
				FromMs: 120000, ToMs: 180000, Author: "user",
			},
			{
				ID: "a7", Kind: "region", Class: "penis", Role: "tracked",
				Rect: []int{540, 470, 160, 200}, AtMs: &at, Author: "auto",
				Confidence: 0.82, Reviewed: &reviewed,
			},
		},
	}
	raw, err := json.Marshal(m)
	if err != nil {
		t.Fatal(err)
	}
	var got SceneMapData
	if err := json.Unmarshal(raw, &got); err != nil {
		t.Fatal(err)
	}
	if !got.HasWindows() || got.Windows[0].Chosen != 88 {
		t.Fatalf("windows: %+v", got.Windows)
	}
	score, err := DecodeSceneMapScore(got.Windows[0].ScoreB64)
	if err != nil || len(score) != 3 || score[1] != 20 {
		t.Fatalf("score decode: %v %v", score, err)
	}
	if got.Marks[1].Confidence != 0.82 || got.Marks[1].Reviewed == nil || *got.Marks[1].Reviewed {
		t.Fatalf("auto mark: %+v", got.Marks[1])
	}
	// Ensure snake_case keys used by the plan schema.
	var probe map[string]any
	if err := json.Unmarshal(raw, &probe); err != nil {
		t.Fatal(err)
	}
	vid, _ := probe["video"].(map[string]any)
	if _, ok := vid["sha256_head"]; !ok {
		t.Fatal("missing sha256_head")
	}
	wins, _ := probe["windows"].([]any)
	w0, _ := wins[0].(map[string]any)
	if _, ok := w0["score_b64"]; !ok {
		t.Fatal("missing score_b64")
	}
	if _, ok := w0["chosen"]; !ok {
		t.Fatal("missing chosen")
	}
}
