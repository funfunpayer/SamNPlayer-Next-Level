package generator

import (
	"context"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/funfunpayer/SamNPlayer/videox"
)

// DumpFrameAt extracts a single PNG frame at timeSec (seconds from start)
// via ffmpeg — no Python. Used for seek-past-black-intro in the GUI.
// ctx may be nil (treated as context.Background by videox.CommandContext),
// but callers should pass a bounded context so a hung/huge-file ffmpeg can
// actually be killed instead of leaking (MT-Infra ctx-kill hygiene).
//
// Strategy: fast input-seek first (-ss before -i). If that yields an empty
// or unreadable PNG (common on some cuts / keyframe-sparse files — Owner
// smoke #335 B), retry with accurate output-seek (-ss after -i). Size is
// always taken from the PNG IHDR when possible; ffprobe is a fallback.
func DumpFrameAt(ctx context.Context, videoPath, outputPNG string, timeSec float64) (width, height int, err error) {
	if timeSec < 0 {
		timeSec = 0
	}
	w, h, err := dumpFrameAttempt(ctx, videoPath, outputPNG, timeSec, false)
	if err == nil {
		return w, h, nil
	}
	firstErr := err
	// Accurate seek is slower but recovers empty/corrupt fast-seek dumps.
	w, h, err = dumpFrameAttempt(ctx, videoPath, outputPNG, timeSec, true)
	if err == nil {
		return w, h, nil
	}
	return 0, 0, fmt.Errorf("%w (accurate retry: %v)", firstErr, err)
}

func dumpFrameAttempt(ctx context.Context, videoPath, outputPNG string, timeSec float64, accurate bool) (width, height int, err error) {
	_ = os.Remove(outputPNG)
	ss := strconv.FormatFloat(timeSec, 'f', 3, 64)
	args := []string{"-v", "error", "-y", "-nostdin"}
	if accurate {
		// Decode then seek — fewer empty frames on sparse keyframe cuts.
		args = append(args, "-i", videoPath, "-ss", ss)
	} else {
		args = append(args, "-ss", ss, "-i", videoPath)
	}
	args = append(args, "-frames:v", "1", "-f", "image2", outputPNG)

	cmd, err := videox.CommandContext(ctx, args...)
	if err != nil {
		return 0, 0, fmt.Errorf("generator: ffmpeg not found")
	}
	out, err := cmd.CombinedOutput()
	if err != nil {
		msg := strings.TrimSpace(string(out))
		if msg != "" {
			return 0, 0, fmt.Errorf("generator: frame at %.2fs failed: %w (%s)", timeSec, err, msg)
		}
		return 0, 0, fmt.Errorf("generator: frame at %.2fs failed: %w", timeSec, err)
	}
	if st, stErr := os.Stat(outputPNG); stErr != nil || st.Size() < 32 {
		size := int64(0)
		if stErr == nil {
			size = st.Size()
		}
		return 0, 0, fmt.Errorf("generator: empty frame at %.2fs (size=%d)", timeSec, size)
	}
	return readPNGSize(ctx, outputPNG)
}

// readPNGSize prefers the PNG IHDR (no ffprobe). Falls back to ffprobe when
// the file is not a PNG or the header is unreadable.
func readPNGSize(ctx context.Context, outputPNG string) (width, height int, err error) {
	if w, h, err2 := pngSize(outputPNG); err2 == nil {
		return w, h, nil
	} else {
		err = err2
	}
	probe, pErr := videox.ProbeCommandContext(ctx, "-v", "error", "-select_streams", "v:0",
		"-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", outputPNG)
	if pErr != nil {
		return 0, 0, fmt.Errorf("generator: unknown frame size: %v / %v", pErr, err)
	}
	info, pErr := probe.CombinedOutput()
	if pErr != nil {
		return 0, 0, fmt.Errorf("generator: unknown frame size: %v / %v", pErr, err)
	}
	parts := strings.Split(strings.TrimSpace(string(info)), "x")
	if len(parts) != 2 {
		return 0, 0, fmt.Errorf("generator: frame size unreadable: %s", string(info))
	}
	w, _ := strconv.Atoi(parts[0])
	h, _ := strconv.Atoi(parts[1])
	if w <= 0 || h <= 0 {
		return 0, 0, fmt.Errorf("generator: invalid frame size %dx%d", w, h)
	}
	return w, h, nil
}

// DumpFirstFrameFast is ffmpeg-based first-frame dump (no Python). Prefer
// this over DumpFirstFrame when Python deps are unavailable.
func DumpFirstFrameFast(ctx context.Context, videoPath, outputPNG string) (width, height int, err error) {
	return DumpFrameAt(ctx, videoPath, outputPNG, 0)
}

func pngSize(path string) (int, int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, 0, err
	}
	defer f.Close()
	var header [24]byte
	if _, err := f.Read(header[:]); err != nil {
		return 0, 0, err
	}
	// PNG IHDR: bytes 16-23 are width/height big-endian.
	if string(header[1:4]) != "PNG" {
		return 0, 0, fmt.Errorf("not a PNG")
	}
	w := int(header[16])<<24 | int(header[17])<<16 | int(header[18])<<8 | int(header[19])
	h := int(header[20])<<24 | int(header[21])<<16 | int(header[22])<<8 | int(header[23])
	if w <= 0 || h <= 0 {
		return 0, 0, fmt.Errorf("PNG size %dx%d", w, h)
	}
	return w, h, nil
}
