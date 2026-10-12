package videox

import (
	"context"
	"os/exec"
)

// CommandContext returns an *exec.Cmd for the resolved ffmpeg binary.
func CommandContext(ctx context.Context, args ...string) (*exec.Cmd, error) {
	bin, err := FFmpeg()
	if err != nil {
		return nil, err
	}
	if ctx == nil {
		ctx = context.Background()
	}
	cmd := exec.CommandContext(ctx, bin, args...)
	hideConsoleWindow(cmd)
	return cmd, nil
}

// ProbeCommandContext returns an *exec.Cmd for the resolved ffprobe binary.
func ProbeCommandContext(ctx context.Context, args ...string) (*exec.Cmd, error) {
	bin, err := FFprobe()
	if err != nil {
		return nil, err
	}
	if ctx == nil {
		ctx = context.Background()
	}
	cmd := exec.CommandContext(ctx, bin, args...)
	hideConsoleWindow(cmd)
	return cmd, nil
}
