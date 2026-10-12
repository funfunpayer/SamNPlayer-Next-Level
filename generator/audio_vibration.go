package generator

import (
	"bufio"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
	"sort"
	"time"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// Audio-only vibration (Owner 9 Oct: "Vibration nur aus dem Audio,
// schaltbar"). The video's loudness drives the vibration channel; suction
// keeps following the script. Play-time only — nothing is written to disk.

// audioVibSampleRate: loudness needs no high band; 4 kHz keeps the ffmpeg
// pipe and the per-window loop cheap even for hour-long videos.
const audioVibSampleRate = 4000

const (
	// audioVibSilenceRMS (~−70 dBFS): below this a window is silence → 0.
	audioVibSilenceRMS = 3e-4
	// audioVibMinRangeDB: minimum dB span between "quiet" and "loud" so a
	// near-constant track does not turn tiny wobbles into full strokes.
	audioVibMinRangeDB = 12.0
	// audioVibGate: normalized values below this become 0 (no idle buzz).
	audioVibGate = 0.08
	// audioVibRelease: EMA on falling values (rise is immediate) so beats
	// feel punchy but do not chop to zero between two hits.
	audioVibRelease = 0.6
)

// AudioVibrationEnvelope decodes the audio track of videoPath and returns a
// 0..1 vibration curve with one value per stepMs (index i ↔ i*stepMs video
// time). Streams ffmpeg output, so memory stays O(duration/stepMs).
// Errors: ffmpeg missing, no readable audio track, or ctx cancelled.
func AudioVibrationEnvelope(ctx context.Context, videoPath string, stepMs int64) ([]float64, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if stepMs <= 0 {
		stepMs = 50
	}
	cmd, err := videox.CommandContext(ctx,
		"-i", videoPath,
		"-vn", "-ac", "1", "-ar", fmt.Sprintf("%d", audioVibSampleRate),
		"-f", "f32le", "-loglevel", "error", "-")
	if err != nil {
		return nil, fmt.Errorf("ffmpeg ist nicht installiert")
	}
	cmd.WaitDelay = 5 * time.Second
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("ffmpeg start: %w", err)
	}
	rms, readErr := windowRMS(bufio.NewReaderSize(stdout, 64<<10), int(int64(audioVibSampleRate)*stepMs/1000))
	waitErr := cmd.Wait()
	if ctxErr := ctx.Err(); ctxErr != nil {
		return nil, ctxErr
	}
	if readErr != nil {
		return nil, readErr
	}
	if waitErr != nil || len(rms) == 0 {
		return nil, fmt.Errorf("Keine Audiospur lesbar")
	}
	return normalizeLoudness(rms), nil
}

// windowRMS reads little-endian float32 samples and returns the RMS of each
// full window. A trailing partial window counts when at least half full.
func windowRMS(r io.Reader, window int) ([]float64, error) {
	if window < 1 {
		window = 1
	}
	var out []float64
	var buf [4]byte
	sum, n := 0.0, 0
	for {
		if _, err := io.ReadFull(r, buf[:]); err != nil {
			if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) {
				break
			}
			return nil, err
		}
		v := float64(math.Float32frombits(binary.LittleEndian.Uint32(buf[:])))
		sum += v * v
		n++
		if n == window {
			out = append(out, math.Sqrt(sum/float64(n)))
			sum, n = 0, 0
		}
	}
	if n*2 >= window {
		out = append(out, math.Sqrt(sum/float64(n)))
	}
	return out, nil
}

// normalizeLoudness maps per-window RMS to 0..1 vibration: dB scale between
// the track's own quiet (20th percentile) and loud (98th percentile) level,
// silence gate, then fast-attack / slow-release smoothing. Adaptive per
// video so quiet and loud masters both use the full device range.
func normalizeLoudness(rms []float64) []float64 {
	out := make([]float64, len(rms))
	db := make([]float64, 0, len(rms))
	for _, v := range rms {
		if v >= audioVibSilenceRMS {
			db = append(db, 20*math.Log10(v))
		}
	}
	if len(db) == 0 {
		return out
	}
	sorted := append([]float64(nil), db...)
	sort.Float64s(sorted)
	lo := percentileSorted(sorted, 0.20)
	hi := percentileSorted(sorted, 0.98)
	if hi-lo < audioVibMinRangeDB {
		lo = hi - audioVibMinRangeDB
	}
	prev := 0.0
	for i, v := range rms {
		x := 0.0
		if v >= audioVibSilenceRMS {
			x = (20*math.Log10(v) - lo) / (hi - lo)
			if x < 0 {
				x = 0
			} else if x > 1 {
				x = 1
			}
		}
		if x < prev {
			x = audioVibRelease*prev + (1-audioVibRelease)*x
		}
		prev = x
		if x < audioVibGate {
			x = 0
		}
		out[i] = x
	}
	return out
}

func percentileSorted(sorted []float64, q float64) float64 {
	if len(sorted) == 0 {
		return 0
	}
	idx := int(math.Round(q * float64(len(sorted)-1)))
	if idx < 0 {
		idx = 0
	} else if idx >= len(sorted) {
		idx = len(sorted) - 1
	}
	return sorted[idx]
}
