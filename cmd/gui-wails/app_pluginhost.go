package main

import (
	"fmt"
	"os/exec"
	"runtime"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/pluginhost"
)

// PluginsDir returns the drop-in Plugins folder path (created if needed).
func (a *App) PluginsDir() string {
	dir, err := pluginhost.EnsurePluginsDir()
	if err != nil {
		return pluginhost.PluginsDirHint()
	}
	return dir
}

// ListInstalledPlugins returns discovered packs under the Plugins folder.
func (a *App) ListInstalledPlugins() ([]pluginhost.Pack, error) {
	dir := a.PluginsDir()
	return pluginhost.DiscoverPacks(dir)
}

// InstallPluginPack copies a folder that contains samn-plugin.json into the
// Plugins directory (file dialog). Generic drop-folder install — no product
// host Enable (Virtual Person product is cancelled / out of scope).
func (a *App) InstallPluginPack() (pluginhost.Pack, error) {
	if a.ctx == nil {
		return pluginhost.Pack{}, fmt.Errorf("app not ready")
	}
	src, err := wailsruntime.OpenDirectoryDialog(a.ctx, wailsruntime.OpenDialogOptions{
		Title: "Select plugin pack folder (must contain samn-plugin.json)",
	})
	if err != nil {
		return pluginhost.Pack{}, err
	}
	if src == "" {
		return pluginhost.Pack{}, nil
	}
	dir, err := pluginhost.EnsurePluginsDir()
	if err != nil {
		return pluginhost.Pack{}, err
	}
	pack, err := pluginhost.InstallPackDir(dir, src)
	if err != nil {
		return pluginhost.Pack{}, err
	}
	logging.Info("app: plugin pack installed", "root", pack.Root, "id", pack.Manifest.ID, "version", pack.Manifest.Version)
	return pack, nil
}

// OpenPluginsFolder opens the Plugins directory in the OS file manager.
func (a *App) OpenPluginsFolder() error {
	dir, err := pluginhost.EnsurePluginsDir()
	if err != nil {
		return err
	}
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("explorer", dir)
	case "darwin":
		cmd = exec.Command("open", dir)
	default:
		cmd = exec.Command("xdg-open", dir)
	}
	return cmd.Start()
}
