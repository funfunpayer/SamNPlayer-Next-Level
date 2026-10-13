package videox

import (
	"archive/zip"
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// Pinned static builds for opt-in EnsureTools (HTTPS only).
// Portable releases also ship these beside the GUI binary so most users
// never need this path.
const (
	ffmpegWinZipURL   = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
	ffmpegLinuxTarURL = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl.tar.xz"
)

func ffmpegDarwinZipURL() string {
	// Apple Silicon vs Intel — BtbN naming.
	if runtime.GOARCH == "arm64" {
		return "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-macosarm64-gpl.zip"
	}
	return "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-macos64-gpl.zip"
}

// EnsureTools installs ffmpeg (+ ffprobe when present) into ToolsDir() when
// they are not already resolvable. Opt-in only — no surprise network calls
// at startup (principle: user must ask).
func EnsureTools(ctx context.Context, onProgress func(string)) error {
	if ctx == nil {
		ctx = context.Background()
	}
	ResetToolCache()
	if Available() {
		p, _ := FFmpeg()
		if onProgress != nil {
			onProgress("ffmpeg already available: " + p)
		}
		return nil
	}
	dir := ToolsDir()
	if dir == "" {
		return fmt.Errorf("cannot determine tools directory")
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	switch runtime.GOOS {
	case "windows":
		return installWindowsZip(ctx, dir, onProgress)
	case "linux":
		return installLinuxTarXZ(ctx, dir, onProgress)
	case "darwin":
		return installDarwinZip(ctx, dir, onProgress)
	default:
		return fmt.Errorf("automatic ffmpeg install is only supported on Windows, Linux, and macOS (got %s) — place ffmpeg next to SamNPlayer or use the portable release", runtime.GOOS)
	}
}

func installWindowsZip(ctx context.Context, destDir string, onProgress func(string)) error {
	return installZipBins(ctx, ffmpegWinZipURL, destDir, onProgress, map[string]string{
		"ffmpeg.exe":  "ffmpeg.exe",
		"ffprobe.exe": "ffprobe.exe",
	})
}

func installDarwinZip(ctx context.Context, destDir string, onProgress func(string)) error {
	return installZipBins(ctx, ffmpegDarwinZipURL(), destDir, onProgress, map[string]string{
		"ffmpeg":  "ffmpeg",
		"ffprobe": "ffprobe",
	})
}

func installZipBins(ctx context.Context, url, destDir string, onProgress func(string), want map[string]string) error {
	if onProgress != nil {
		onProgress("Downloading ffmpeg…")
	}
	tmp, err := downloadToTemp(ctx, url, ".zip")
	if err != nil {
		return err
	}
	defer os.Remove(tmp)
	if onProgress != nil {
		onProgress("Extracting ffmpeg…")
	}
	r, err := zip.OpenReader(tmp)
	if err != nil {
		return err
	}
	defer r.Close()
	found := 0
	for _, f := range r.File {
		if f.FileInfo().IsDir() {
			continue
		}
		base := filepath.Base(f.Name)
		dstName, ok := want[strings.ToLower(base)]
		if !ok {
			for k, v := range want {
				if strings.EqualFold(base, k) {
					dstName, ok = v, true
					break
				}
			}
		}
		if !ok {
			continue
		}
		if err := extractZipFile(f, filepath.Join(destDir, dstName)); err != nil {
			return err
		}
		found++
	}
	if found == 0 {
		return fmt.Errorf("ffmpeg binary not found inside download")
	}
	return finishInstall(onProgress)
}

func extractZipFile(f *zip.File, dst string) error {
	rc, err := f.Open()
	if err != nil {
		return err
	}
	defer rc.Close()
	out, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o755)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, rc)
	return err
}

func installLinuxTarXZ(ctx context.Context, destDir string, onProgress func(string)) error {
	if _, err := exec.LookPath("tar"); err != nil {
		return fmt.Errorf("tar not found — cannot extract ffmpeg archive; use the portable release instead")
	}
	if onProgress != nil {
		onProgress("Downloading ffmpeg…")
	}
	tmp, err := downloadToTemp(ctx, ffmpegLinuxTarURL, ".tar.xz")
	if err != nil {
		return fmt.Errorf("ffmpeg download failed (%w). Use the portable release (ffmpeg next to SamNPlayer) or: sudo apt install ffmpeg", err)
	}
	defer os.Remove(tmp)

	extractDir, err := os.MkdirTemp("", "samnplayer-ffmpeg-extract-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(extractDir)

	if onProgress != nil {
		onProgress("Extracting ffmpeg…")
	}
	cmd := exec.CommandContext(ctx, "tar", "-xJf", tmp, "-C", extractDir)
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("tar extract: %w (%s)", err, strings.TrimSpace(string(out)))
	}
	copied := 0
	err = filepath.Walk(extractDir, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if info.IsDir() {
			return nil
		}
		base := info.Name()
		if base != "ffmpeg" && base != "ffprobe" {
			return nil
		}
		if copyErr := copyFile(path, filepath.Join(destDir, base), 0o755); copyErr != nil {
			return copyErr
		}
		copied++
		return nil
	})
	if err != nil {
		return err
	}
	if copied == 0 || !okFile(filepath.Join(destDir, "ffmpeg")) {
		return fmt.Errorf("ffmpeg binary not found inside archive")
	}
	return finishInstall(onProgress)
}

func copyFile(src, dst string, mode os.FileMode) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	defer out.Close()
	if _, err := io.Copy(out, in); err != nil {
		return err
	}
	return out.Chmod(mode)
}

func finishInstall(onProgress func(string)) error {
	ResetToolCache()
	p, err := FFmpeg()
	if err != nil {
		return err
	}
	if onProgress != nil {
		onProgress("Installed: " + p)
	}
	return nil
}

func downloadToTemp(ctx context.Context, url, suffix string) (string, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return "", err
	}
	client := &http.Client{Timeout: 15 * time.Minute}
	res, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return "", fmt.Errorf("download: HTTP %d", res.StatusCode)
	}
	tmp, err := os.CreateTemp("", "samnplayer-ffmpeg-*"+suffix)
	if err != nil {
		return "", err
	}
	defer tmp.Close()
	if _, err := io.Copy(tmp, res.Body); err != nil {
		os.Remove(tmp.Name())
		return "", err
	}
	return tmp.Name(), nil
}
