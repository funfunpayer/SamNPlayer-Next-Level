// Package legal carries the end-user terms shipped with SamNPlayer:
// the EULA (freeware, not open source), the third-party notices, and the
// donation links. Owner decision 10 Oct 2026: no licence keys — free with
// an EULA, financed by donations.
package legal

import (
	_ "embed"
	"strings"
)

// EULAVersion identifies the EULA text. Bump it when EULA.md changes in a
// way users must accept again; the GUI asks once per version.
const EULAVersion = "2026-10-11"

//go:embed EULA.md
var eulaText string

//go:embed THIRD_PARTY_NOTICES.md
var noticesText string

// EULA returns the end-user licence agreement (Markdown).
func EULA() string { return eulaText }

// ThirdPartyNotices returns the third-party licence notices (Markdown).
func ThirdPartyNotices() string { return noticesText }

// DonateLink is one way to support SamNPlayer.
type DonateLink struct {
	Name string `json:"name"`
	URL  string `json:"url"`
}

// Donation pages (Owner: Patreon or PayPal). An empty URL hides that button.
const (
	PatreonURL = ""
	PayPalURL  = ""
)

// DonateLinks returns the configured donation pages (only those with a URL).
func DonateLinks() []DonateLink {
	var out []DonateLink
	for _, l := range []DonateLink{{"Patreon", PatreonURL}, {"PayPal", PayPalURL}} {
		if strings.HasPrefix(l.URL, "https://") {
			out = append(out, l)
		}
	}
	return out
}

// Partner site shown in Settings → About (Owner 11 Oct: the "Sex for him"
// info site; the URL follows — empty = "coming soon", no link).
const (
	PartnerSiteName = "Sex for him"
	PartnerSiteURL  = ""
)
