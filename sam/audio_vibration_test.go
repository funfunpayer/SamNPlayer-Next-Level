package sam

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestApplyAudioVibrationKeepsSuctionAndShiftsByOffset(t *testing.T) {
	frames := []funscript.Frame{
		{At: 0, Vibration: 0.9, Suction: 0.1},
		{At: 50, Vibration: 0.9, Suction: 0.2},
		{At: 100, Vibration: 0.9, Suction: 0.3},
	}
	env := []float64{0, 0.5, 1, 0.25, 0, 0}
	out := ApplyAudioVibration(frames, env, 50, 0, 1)
	for i, want := range []float64{0, 0.5, 1} {
		if out[i].Vibration != want {
			t.Fatalf("frame %d vib %v, want %v (audio, not script)", i, out[i].Vibration, want)
		}
		if out[i].Suction != frames[i].Suction {
			t.Fatalf("frame %d suction changed: %v", i, out[i].Suction)
		}
	}
	// Offset +50: script t=0 plays at video 50 ms.
	shifted := ApplyAudioVibration(frames, env, 50, 50, 1)
	if shifted[0].Vibration != 0.5 || shifted[2].Vibration != 0.25 {
		t.Fatalf("offset not applied: %+v", shifted[:3])
	}
	if frames[0].Vibration != 0.9 {
		t.Fatal("input frames must not be modified")
	}
}

func TestApplyAudioVibrationExtendsToAudioEnd(t *testing.T) {
	frames := []funscript.Frame{{At: 0, Suction: 0.4}, {At: 50, Suction: 0.4}}
	env := []float64{0, 0, 0, 0.8, 0}
	out := ApplyAudioVibration(frames, env, 50, 0, 0.5)
	if len(out) != 5 || out[4].At != 200 {
		t.Fatalf("want frames up to audio end 200 ms, got %+v", out)
	}
	if out[3].Vibration != 0.4 || out[3].Suction != 0 {
		t.Fatalf("extended frame: scaled audio vib, no suction; got %+v", out[3])
	}
}

func TestApplyAudioVibrationNoEnvelopeIsNoop(t *testing.T) {
	frames := []funscript.Frame{{At: 0, Vibration: 0.3}}
	if out := ApplyAudioVibration(frames, nil, 50, 0, 1); out[0].Vibration != 0.3 {
		t.Fatal("empty envelope must keep script vibration")
	}
}
