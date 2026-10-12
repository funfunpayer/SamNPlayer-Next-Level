package pluginhost

import (
	"os"
	"path/filepath"
	"runtime"
)

// DefaultPluginsDir returns the user Plugins folder:
//
//	Windows: %APPDATA%\SamNPlayer\plugins  (via os.UserConfigDir)
//	macOS/Linux: ~/.config/SamNPlayer/plugins
//
// Empty string if the config dir is unavailable.
func DefaultPluginsDir() string {
	base, err := os.UserConfigDir()
	if err != nil || base == "" {
		return ""
	}
	return filepath.Join(base, "SamNPlayer", "plugins")
}

// EnsurePluginsDir creates the default plugins directory when possible.
func EnsurePluginsDir() (string, error) {
	dir := DefaultPluginsDir()
	if dir == "" {
		return "", os.ErrNotExist
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return dir, err
	}
	return dir, nil
}

// Portable hint for docs — same as DefaultPluginsDir on this OS.
func PluginsDirHint() string {
	dir := DefaultPluginsDir()
	if dir != "" {
		return dir
	}
	if runtime.GOOS == "windows" {
		return `%APPDATA%\SamNPlayer\plugins`
	}
	return "~/.config/SamNPlayer/plugins"
}
