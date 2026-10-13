package generator

import (
	"bytes"
	"context"
	"encoding/binary"
	"math"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/funfunpayer/SamNPlayer/videox"
)

func f32le(samples ...float32) []byte {
	var buf bytes.Buffer
	for _, s := range samples {
		_ = binary.Write(&buf, binary.LittleEndian, s)
	}
	return buf.Bytes()
}

func TestWindowRMS(t *testing.T) {
	// window 2: [3,4] → rms 3.5355, [0,0] → 0, trailing [1] (half full) → 1
	got, err := windowRMS(bytes.NewReader(f32le(3, 4, 0, 0, 1)), 2)
	if err != nil {
		t.Fatal(err)
	}
	want := []float64{math.Sqrt(12.5), 0, 1}
	if len(got) != len(want) {
		t.Fatalf("got %v, want %v", got, want)
	}
	for i := range want {
		if math.Abs(got[i]-want[i]) > 1e-6 {
			t.Fatalf("window %d: got %v, want %v", i, got[i], want[i])
		}
	}
}

func TestNormalizeLoudnessSilenceLoudAndRelease(t *testing.T) {
	rms := make([]float64, 0, 60)
	for i := 0; i < 20; i++ {
		rms = append(rms, 0) // digital silence
	}
	for i := 0; i < 20; i++ {
		rms = append(rms, 0.5) // loud
	}
	for i := 0; i < 20; i++ {
		rms = append(rms, 0.005) // quiet (−46 dBFS vs −6)
	}
	out := normalizeLoudness(rms)
	if out[0] != 0 || out[19] != 0 {
		t.Fatalf("silence must not vibrate: %v", out[:20])
	}
	if out[20] < 0.95 {
		t.Fatalf("loud must reach ~full strength immediately (fast attack): %v", out[20])
	}
	if !(out[40] > 0 && out[40] < out[39]) {
		t.Fatalf("release must fall off smoothly, not chop: loud %v → %v", out[39], out[40])
	}
	if out[59] != 0 {
		t.Fatalf("long quiet tail must gate to 0, got %v", out[59])
	}
	for i, v := range out {
		if v < 0 || v > 1 {
			t.Fatalf("value %d out of range: %v", i, v)
		}
	}
}

func TestNormalizeLoudnessAllSilent(t *testing.T) {
	out := normalizeLoudness([]float64{0, 1e-6, 0})
	for _, v := range out {
		if v != 0 {
			t.Fatalf("all-silent track must stay 0: %v", out)
		}
	}
}

// End-to-end with real ffmpeg: 1 s silence, 1 s tone → vibration off, then on.
func TestAudioVibrationEnvelopeFromVideo(t *testing.T) {
	bin, err := videox.FFmpeg()
	if err != nil {
		t.Skip("ffmpeg not available")
	}
	path := filepath.Join(t.TempDir(), "beep.wav")
	cmd := exec.Command(bin, "-loglevel", "error",
		"-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono:d=1",
		"-f", "lavfi", "-i", "sine=frequency=220:sample_rate=8000:duration=1",
		"-filter_complex", "[0][1]concat=n=2:v=0:a=1", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Skipf("ffmpeg lavfi unavailable: %v %s", err, out)
	}
	env, err := AudioVibrationEnvelope(context.Background(), path, 50)
	if err != nil {
		t.Fatal(err)
	}
	if len(env) < 38 || len(env) > 42 {
		t.Fatalf("2 s at 50 ms → ~40 values, got %d", len(env))
	}
	if env[5] != 0 || env[15] != 0 {
		t.Fatalf("silent first second must not vibrate: %v", env[:20])
	}
	if env[30] < 0.9 {
		t.Fatalf("tone second must vibrate strongly, got %v", env[30])
	}
}

func TestAudioVibrationEnvelopeCancelled(t *testing.T) {
	if _, err := videox.FFmpeg(); err != nil {
		t.Skip("ffmpeg not available")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := AudioVibrationEnvelope(ctx, "/nonexistent.mp4", 50); err == nil {
		t.Fatal("cancelled context must return an error")
	}
}

func TestEstimateVideoAudioTempoHzClickTrack(t *testing.T) {
	bin, err := videox.FFmpeg()
	if err != nil {
		t.Skip("ffmpeg not available")
	}
	path := filepath.Join(t.TempDir(), "clicks.wav")
	// 100 ms beep every 500 ms for 20 s → 2 Hz.
	cmd := exec.Command(bin, "-loglevel", "error", "-f", "lavfi",
		"-i", "aevalsrc=sin(2*PI*440*t)*lt(mod(t\\,0.5)\\,0.1):s=8000:d=20", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Skipf("ffmpeg lavfi unavailable: %v %s", err, out)
	}
	hz, err := EstimateVideoAudioTempoHz(context.Background(), path)
	if err != nil {
		t.Fatal(err)
	}
	if math.Abs(hz-2) > 0.1 {
		t.Fatalf("click track at 2 Hz read as %.3f Hz", hz)
	}
}
