package main

import (
	"fmt"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/edition"
	"github.com/funfunpayer/SamNPlayer/legal"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/update"
)

// prefEULAAccepted stores the EULA version the user accepted. The GUI asks
// again only when legal.EULAVersion changes.
const prefEULAAccepted = "eula.accepted_version"

// EULAInfo is what the first-start dialog and Settings show.
type EULAInfo struct {
	Version  string `json:"version"`
	Text     string `json:"text"`
	Accepted bool   `json:"accepted"`
}

// GetEULA returns the EULA and whether the current version was accepted.
func (a *App) GetEULA() EULAInfo {
	accepted := a.settings != nil && a.settings.GetString(prefEULAAccepted, "") == legal.EULAVersion
	return EULAInfo{Version: legal.EULAVersion, Text: legal.EULA(), Accepted: accepted}
}

// AcceptEULA records acceptance of the given EULA version. A stale version
// (the text changed while the dialog was open) is refused.
func (a *App) AcceptEULA(version string) error {
	if version != legal.EULAVersion {
		return fmt.Errorf("EULA version %q is not current (%s)", version, legal.EULAVersion)
	}
	if err := a.settings.Set(prefEULAAccepted, version); err != nil {
		return err
	}
	logging.Info("eula: accepted", "version", version)
	return nil
}

// DeclineEULA closes the app (the user did not accept the terms).
func (a *App) DeclineEULA() {
	logging.Info("eula: declined — quitting")
	if a.ctx != nil {
		runtime.Quit(a.ctx)
	}
}

// GetThirdPartyNotices returns the third-party licence notices.
func (a *App) GetThirdPartyNotices() string { return legal.ThirdPartyNotices() }

// GetDonateLinks returns the configured donation pages (may be empty).
func (a *App) GetDonateLinks() []legal.DonateLink { return legal.DonateLinks() }

// OpenDonateLink opens one of the configured donation pages in the browser.
// Only URLs from legal.DonateLinks are accepted.
func (a *App) OpenDonateLink(url string) error {
	for _, l := range legal.DonateLinks() {
		if l.URL == url {
			runtime.BrowserOpenURL(a.ctx, url)
			return nil
		}
	}
	return fmt.Errorf("unknown donation link")
}

// AboutInfo feeds Settings → About SamNPlayer.
type AboutInfo struct {
	Version     string `json:"version"`
	Edition     string `json:"edition"`
	EULAVersion string `json:"eulaVersion"`
	PartnerName string `json:"partnerName"`
	PartnerURL  string `json:"partnerUrl"`
}

// GetAboutInfo returns version, edition and the partner site (URL may be empty).
func (a *App) GetAboutInfo() AboutInfo {
	return AboutInfo{
		Version:     update.Describe(),
		Edition:     edition.Name(),
		EULAVersion: legal.EULAVersion,
		PartnerName: legal.PartnerSiteName,
		PartnerURL:  legal.PartnerSiteURL,
	}
}

// OpenPartnerSite opens the partner website when its URL is set.
func (a *App) OpenPartnerSite() error {
	if legal.PartnerSiteURL == "" {
		return fmt.Errorf("partner website coming soon")
	}
	runtime.BrowserOpenURL(a.ctx, legal.PartnerSiteURL)
	return nil
}
