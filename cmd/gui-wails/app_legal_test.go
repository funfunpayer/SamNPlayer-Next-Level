package main

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/legal"
)

func TestEULAAcceptOncePerVersion(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	info := a.GetEULA()
	if info.Accepted || info.Version != legal.EULAVersion || info.Text == "" {
		t.Fatalf("fresh install must ask: %+v", info.Accepted)
	}
	if err := a.AcceptEULA("1999-01-01"); err == nil {
		t.Fatal("a stale version must be refused")
	}
	if err := a.AcceptEULA(legal.EULAVersion); err != nil {
		t.Fatal(err)
	}
	if !a.GetEULA().Accepted {
		t.Fatal("accepted version not stored")
	}
	if !NewApp().GetEULA().Accepted {
		t.Fatal("acceptance must survive a restart")
	}
}

func TestOpenDonateLinkRefusesUnknownURL(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	if err := NewApp().OpenDonateLink("https://evil.example"); err == nil {
		t.Fatal("only configured donation links may be opened")
	}
}

func TestAboutInfo(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	a := NewApp()
	info := a.GetAboutInfo()
	if info.Version == "" || info.Edition == "" || info.EULAVersion != legal.EULAVersion || info.PartnerName == "" {
		t.Fatalf("about info incomplete: %+v", info)
	}
	if legal.PartnerSiteURL == "" {
		if err := a.OpenPartnerSite(); err == nil {
			t.Fatal("without a URL the partner link must say coming soon")
		}
	}
}
