package videox

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// PlayableCodecs are typically decoded by Chromium/WebView2 and WebKitGTK.
var PlayableCodecs = map[string]bool{
	"h264": true,
	"avc1": true,
	"vp8":  true,
	"vp9":  true,
	"av1":  true,
	"vp09": true,
	"av01": true,
}

// SoftProxyMaxWidth caps re-encoded proxy width with a soft Lanczos downscale.
// Upscaling in the proxy is intentionally not done — measured WebGL/canvas
// "sharpen/upscale" paths were worse than native <video> CSS bilinear.
const SoftProxyMaxWidth = 1920

// PlayableContainers are usually fine in the embedded <video> when the
// codec is also playable. Other containers get a remux/proxy even for H.264.
var PlayableContainers = map[string]bool{
	".mp4":  true,
	".m4v":  true,
	".webm": true,
	".mov":  true,
}

// LikelyPlayable reports whether Info.Codec is usually playable in the
// embedded webview <video> element without a proxy.
func (i Info) LikelyPlayable() bool {
	c := strings.ToLower(strings.TrimSpace(i.Codec))
	if c == "" {
		return false
	}
	if PlayableCodecs[c] {
		return true
	}
	// Some probes report long names.
	for _, p := range []string{"h264", "avc", "vp8", "vp9", "av1"} {
		if strings.Contains(c, p) {
			return true
		}
	}
	return false
}

// ProxyPath returns a sibling cache path for an H.264/AAC MP4 proxy.
func ProxyPath(src string) string {
	dir := filepath.Dir(src)
	base := strings.TrimSuffix(filepath.Base(src), filepath.Ext(src))
	return filepath.Join(dir, "."+base+".samnplayer-h264.mp4")
}

func isH264Family(codec string) bool {
	c := strings.ToLower(strings.TrimSpace(codec))
	return c == "h264" || c == "avc1" || strings.Contains(c, "h264") || strings.Contains(c, "avc")
}

// EnsurePlayableProxy returns src if already likely playable, otherwise
// creates (or reuses) an H.264+AAC MP4 via ffmpeg next to the source.
// Prefers a lossless remux (-c copy) when the video is already H.264 in a
// awkward container; otherwise re-encodes with soft Lanczos downscale when
// wider than SoftProxyMaxWidth. Requires ffmpeg on PATH. onProgress may be nil.
func EnsurePlayableProxy(ctx context.Context, src string, onProgress func(string)) (outPath string, converted bool, err error) {
	info, err := Probe(ctx, src)
	if err != nil {
		return "", false, fmt.Errorf("probe: %w", err)
	}
	if info.LikelyPlayable() {
		ext := strings.ToLower(filepath.Ext(src))
		if PlayableContainers[ext] {
			return src, false, nil
		}
	}
	if _, err := FFmpeg(); err != nil {
		return "", false, fmt.Errorf("ffmpeg missing — codec %q likely not playable (use portable zip or Settings → Install video tools)", info.Codec)
	}
	dst := ProxyPath(src)

	// Single-owner decode (MT-Infra): dst is a deterministic sibling path
	// with no lock before this — two overlapping calls for the same src
	// (e.g. a double-click on "Make playable" before the first finishes)
	// would otherwise launch two ffmpeg writers on the same dst file.
	// Concurrent callers for the same dst wait for the first and share its
	// result instead.
	return singleflightProxy(dst, func() (string, bool, error) {
		if st, err := os.Stat(dst); err == nil && !st.IsDir() && st.Size() > 1024 {
			// Reuse if newer than source.
			srcSt, _ := os.Stat(src)
			if srcSt == nil || !srcSt.ModTime().After(st.ModTime()) {
				if onProgress != nil {
					onProgress("Existing playable copy: " + filepath.Base(dst))
				}
				return dst, true, nil
			}
		}

		// Prefer remux when video is already H.264 (no quality loss, much faster).
		if isH264Family(info.Codec) {
			if onProgress != nil {
				onProgress(fmt.Sprintf("Remux to MP4 (stream copy, %s)…", info.Codec))
			}
			if err := remuxCopyMP4(ctx, src, dst); err == nil {
				if onProgress != nil {
					onProgress("Done (remux): " + filepath.Base(dst))
				}
				return dst, true, nil
			}
			_ = os.Remove(dst)
			if onProgress != nil {
				onProgress("Remux failed — re-encoding…")
			}
		}

		if onProgress != nil {
			onProgress(fmt.Sprintf("Converting to H.264/AAC (%s → mp4)…", info.Codec))
		}
		args := []string{
			"-y", "-hide_banner", "-loglevel", "error",
			"-i", src,
			"-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
			"-pix_fmt", "yuv420p",
		}
		w, _ := info.Rotated()
		if w > SoftProxyMaxWidth {
			// Soft Lanczos downscale only — never upscale in the proxy.
			args = append(args, "-vf", fmt.Sprintf("scale='min(%d,iw)':-2:flags=lanczos", SoftProxyMaxWidth))
		}
		args = append(args,
			"-c:a", "aac", "-b:a", "160k",
			"-movflags", "+faststart",
			dst,
		)
		cmd, err := CommandContext(ctx, args...)
		if err != nil {
			return "", false, err
		}
		if b, err := cmd.CombinedOutput(); err != nil {
			_ = os.Remove(dst)
			msg := strings.TrimSpace(string(b))
			if msg == "" {
				msg = err.Error()
			}
			return "", false, fmt.Errorf("ffmpeg: %s", msg)
		}
		if onProgress != nil {
			onProgress("Fertig: " + filepath.Base(dst))
		}
		return dst, true, nil
	})
}

var (
	proxyFlightMu sync.Mutex
	proxyFlight   = map[string]*proxyCall{}
)

// proxyCall is the in-flight/most-recent result for one dst path.
type proxyCall struct {
	wg        sync.WaitGroup
	outPath   string
	converted bool
	err       error
}

// singleflightProxy runs fn for dst, or — if another goroutine is already
// running it for the same dst — waits and returns that call's result
// instead of starting a second ffmpeg process on the same output file.
func singleflightProxy(dst string, fn func() (string, bool, error)) (string, bool, error) {
	proxyFlightMu.Lock()
	if c, ok := proxyFlight[dst]; ok {
		proxyFlightMu.Unlock()
		c.wg.Wait()
		return c.outPath, c.converted, c.err
	}
	c := &proxyCall{}
	c.wg.Add(1)
	proxyFlight[dst] = c
	proxyFlightMu.Unlock()

	c.outPath, c.converted, c.err = fn()

	proxyFlightMu.Lock()
	delete(proxyFlight, dst)
	proxyFlightMu.Unlock()
	c.wg.Done()
	return c.outPath, c.converted, c.err
}

func remuxCopyMP4(ctx context.Context, src, dst string) error {
	cmd, err := CommandContext(ctx,
		"-y", "-hide_banner", "-loglevel", "error",
		"-i", src,
		"-c", "copy",
		"-movflags", "+faststart",
		dst,
	)
	if err != nil {
		return err
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		msg := strings.TrimSpace(string(b))
		if msg == "" {
			msg = err.Error()
		}
		return fmt.Errorf("remux: %s", msg)
	}
	return nil
}

// ProbeTimeout is the default probe budget for UI calls.
const ProbeTimeout = 8 * time.Second
