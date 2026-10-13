package videox

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os/exec"
	"strconv"
	"strings"
	"sync"
)

// GrayFrame is a single 8-bit grayscale frame. Pixels has len Width*Height.
type GrayFrame struct {
	Index  int
	Width  int
	Height int
	Pixels []byte
}

// GrayReaderOptions controls decoding.
type GrayReaderOptions struct {
	// FPS resamples the stream to a fixed rate. 0 keeps the source rate.
	FPS float64
	// MaxWidth downscales so the long edge stays at or below this value.
	// 0 disables scaling. Analysis rarely benefits above ~640.
	MaxWidth int
	// AutoRotate applies container rotation metadata. Default true.
	AutoRotate bool
	// StartSec seeks before decoding (ffmpeg -ss before -i).
	StartSec float64
}

// FrameReader streams grayscale frames from ffmpeg over a pipe.
type FrameReader struct {
	Width  int
	Height int

	cmd    *exec.Cmd
	out    *bufio.Reader
	stderr *ringBuffer
	cancel context.CancelFunc

	once     sync.Once
	closeErr error
}

// NewGrayReader starts ffmpeg and returns a reader for decoded frames.
//
// Differences to the original:
//   - scaling is supported, so analysis no longer runs on full 1080p buffers
//   - -an drops audio decoding entirely
//   - stderr is captured, so a failing ffmpeg reports a real message
//   - Close cancels the process instead of blocking in Wait forever
//   - the output geometry is computed here, not taken on faith from the caller
func NewGrayReader(ctx context.Context, path string, info Info, opt GrayReaderOptions) (*FrameReader, error) {
	if opt.MaxWidth < 0 {
		opt.MaxWidth = 0
	}

	w, h := info.Width, info.Height
	if opt.AutoRotate {
		w, h = info.Rotated()
	}
	w, h = fitWidth(w, h, opt.MaxWidth)
	if w <= 0 || h <= 0 {
		return nil, fmt.Errorf("invalid target geometry %dx%d", w, h)
	}

	var filters []string
	if opt.FPS > 0 {
		filters = append(filters, "fps="+strconv.FormatFloat(opt.FPS, 'f', 4, 64))
	}
	filters = append(filters, fmt.Sprintf("scale=%d:%d:flags=lanczos", w, h))
	filters = append(filters, "format=gray")

	args := []string{"-v", "error", "-nostdin"}
	if !opt.AutoRotate {
		args = append(args, "-noautorotate")
	}
	if opt.StartSec > 0 {
		args = append(args, "-ss", strconv.FormatFloat(opt.StartSec, 'f', 3, 64))
	}
	args = append(args,
		"-i", path,
		"-an", "-sn", "-dn",
		"-vf", strings.Join(filters, ","),
		"-f", "rawvideo",
		"-pix_fmt", "gray",
		"-",
	)

	runCtx, cancel := context.WithCancel(ctx)
	cmd, err := CommandContext(runCtx, args...)
	if err != nil {
		cancel()
		return nil, err
	}

	stderr := &ringBuffer{limit: 8 << 10}
	cmd.Stderr = stderr

	pipe, err := cmd.StdoutPipe()
	if err != nil {
		cancel()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		cancel()
		return nil, fmt.Errorf("start ffmpeg: %w", err)
	}

	return &FrameReader{
		Width:  w,
		Height: h,
		cmd:    cmd,
		out:    bufio.NewReaderSize(pipe, w*h*2),
		stderr: stderr,
		cancel: cancel,
	}, nil
}

// Next reads the next full frame. It returns io.EOF at the end of the stream.
//
// A short read at the end is reported as io.EOF rather than
// ErrUnexpectedEOF, because ffmpeg may be killed mid-frame on cancellation.
func (r *FrameReader) Next(index int) (GrayFrame, error) {
	buf := make([]byte, r.Width*r.Height)
	if _, err := io.ReadFull(r.out, buf); err != nil {
		if errors.Is(err, io.ErrUnexpectedEOF) {
			return GrayFrame{}, io.EOF
		}
		return GrayFrame{}, err
	}
	return GrayFrame{Index: index, Width: r.Width, Height: r.Height, Pixels: buf}, nil
}

// Close terminates ffmpeg and reports any decoder error.
//
// It is safe to call Close before the stream is exhausted; the original
// implementation deadlocked in that case because it only called Wait.
//
// After a normal EOF drain, Close still cancels the CommandContext. That
// makes Wait return context.Canceled even when ffmpeg already finished
// cleanly — treat cancel/deadline as success unless stderr shows a real
// decoder failure (review finding: TestGrayReaderScalesAndCounts).
func (r *FrameReader) Close() error {
	r.once.Do(func() {
		r.cancel()
		err := r.cmd.Wait()
		if err == nil {
			return
		}
		if msg := strings.TrimSpace(r.stderr.String()); msg != "" {
			r.closeErr = fmt.Errorf("ffmpeg: %s", msg)
			return
		}
		if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
			return
		}
		var exit *exec.ExitError
		if errors.As(err, &exit) && runCancelled(r.cmd) {
			return
		}
		r.closeErr = err
	})
	return r.closeErr
}

// Stderr returns whatever ffmpeg wrote to stderr so far.
func (r *FrameReader) Stderr() string { return strings.TrimSpace(r.stderr.String()) }

func runCancelled(cmd *exec.Cmd) bool {
	if cmd.ProcessState == nil {
		return false
	}
	return !cmd.ProcessState.Exited()
}

// fitWidth scales w/h down so w <= maxWidth, keeping both dimensions even.
func fitWidth(w, h, maxWidth int) (int, int) {
	if maxWidth <= 0 || w <= maxWidth {
		return even(w), even(h)
	}
	scale := float64(maxWidth) / float64(w)
	return even(maxWidth), even(int(float64(h)*scale + 0.5))
}

func even(v int) int {
	if v < 2 {
		return 2
	}
	if v%2 == 1 {
		return v - 1
	}
	return v
}

// ringBuffer keeps the last N bytes written to it.
type ringBuffer struct {
	mu    sync.Mutex
	buf   []byte
	limit int
}

func (b *ringBuffer) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.buf = append(b.buf, p...)
	if len(b.buf) > b.limit {
		b.buf = b.buf[len(b.buf)-b.limit:]
	}
	return len(p), nil
}

func (b *ringBuffer) String() string {
	b.mu.Lock()
	defer b.mu.Unlock()
	return string(b.buf)
}
