package main

import (
	"fmt"

	"github.com/funfunpayer/SamNPlayer/edition"
	"github.com/funfunpayer/SamNPlayer/update"
)

// guiAssetKind is the release asset this build updates from: each edition
// only ever replaces itself with the same edition.
func guiAssetKind() string {
	if edition.Suite() {
		return "suite-gui"
	}
	return "gui"
}

// UpdateCheckResult is shared by full Releases and the additive Patch channel.
// Kind is ""/"release" for full updates and "patch" for hotfixes.
type UpdateCheckResult struct {
	Available bool            `json:"available"`
	Kind      string          `json:"kind,omitempty"` // "release" | "patch"
	Release   *update.Release `json:"release,omitempty"`
	Patch     *update.Patch   `json:"patch,omitempty"`
	PatchID   string          `json:"patchId,omitempty"`
	AssetName string          `json:"assetName,omitempty"`
	Error     string          `json:"error,omitempty"`
}

func (a *App) CheckForUpdate() UpdateCheckResult {
	rel, err := update.CheckLatest()
	if err != nil {
		// No public release yet → "up to date", not a toast/popup-worthy error.
		if err == update.ErrNoRelease {
			return UpdateCheckResult{Available: false}
		}
		return UpdateCheckResult{Error: err.Error()}
	}
	if !update.IsNewer(update.Version, rel.TagName) {
		return UpdateCheckResult{Available: false}
	}
	asset, ok := rel.AssetForThisPlatform(guiAssetKind())
	if !ok {
		return UpdateCheckResult{Available: true, Kind: "release", Release: rel, Error: "no matching binary for this platform in the release"}
	}
	return UpdateCheckResult{Available: true, Kind: "release", Release: rel, AssetName: asset.Name}
}

// CheckForPatch looks for an unapplied hotfix for the current base version.
// Prefer CheckForUpdate first: a full release always wins over a patch.
func (a *App) CheckForPatch() UpdateCheckResult {
	p, err := update.CheckLatestPatch(update.CurrentBaseVersion())
	if err != nil {
		if err == update.ErrNoRelease {
			return UpdateCheckResult{Available: false}
		}
		return UpdateCheckResult{Error: err.Error()}
	}
	if p == nil {
		return UpdateCheckResult{Available: false}
	}
	asset, ok := p.AssetForThisPlatform(guiAssetKind())
	if !ok {
		return UpdateCheckResult{
			Available: true,
			Kind:      "patch",
			Patch:     p,
			PatchID:   p.ID,
			Error:     "no matching binary for this platform in the patch",
		}
	}
	return UpdateCheckResult{
		Available: true,
		Kind:      "patch",
		Patch:     p,
		PatchID:   p.ID,
		AssetName: asset.Name,
	}
}

// ApplyUpdate lädt das zur aktuellen Plattform passende Asset herunter
// (mit Prüfsummen-Verifikation, siehe update.Download) und startet das
// Programm damit neu. Kehrt im Erfolgsfall nicht zurück (Prozessende).
func (a *App) ApplyUpdate() error {
	rel, err := update.CheckLatest()
	if err != nil {
		return err
	}
	asset, ok := rel.AssetForThisPlatform(guiAssetKind())
	if !ok {
		return fmt.Errorf("no matching binary for this platform in the release")
	}
	checksum, checksumErr := rel.ChecksumFor(asset.Name)
	if checksumErr != nil {
		checksum = "" // Download() protokolliert und läuft ohne Verifikation weiter
	}
	path, err := update.Download(asset, checksum)
	if err != nil {
		return err
	}
	return update.ApplyAndRestart(path)
}

// ApplyPatch downloads the latest applicable patch (same safety as ApplyUpdate)
// and restarts. Marks the patch applied before replacing the binary.
func (a *App) ApplyPatch() error {
	p, err := update.CheckLatestPatch(update.CurrentBaseVersion())
	if err != nil {
		return err
	}
	if p == nil {
		return fmt.Errorf("no applicable patch for this version")
	}
	asset, ok := p.AssetForThisPlatform(guiAssetKind())
	if !ok {
		return fmt.Errorf("no matching binary for this platform in the patch")
	}
	checksum, checksumErr := p.ChecksumFor(asset.Name)
	if checksumErr != nil {
		checksum = ""
	}
	path, err := update.Download(asset, checksum)
	if err != nil {
		return err
	}
	// Persist before exit so a successful download+restart is not re-offered.
	if err := update.MarkPatchApplied(p.ID); err != nil {
		// Non-fatal: still apply; worst case the banner reappears once.
		_ = err
	}
	return update.ApplyAndRestart(path)
}

func (a *App) CurrentVersion() string {
	return update.Describe()
}
