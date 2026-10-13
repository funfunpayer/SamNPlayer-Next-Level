package update

import (
	"encoding/json"
	"runtime"
	"strings"
	"testing"
)

func TestDescribe(t *testing.T) {
	got := Describe()
	if got == "" {
		t.Fatal("empty describe")
	}
	if Version == "dev" && got != BaseVersion+"-dev" {
		t.Fatalf("dev describe=%q want %s-dev", got, BaseVersion)
	}
}

func TestErrNoReleaseSentinel(t *testing.T) {
	if ErrNoRelease == nil {
		t.Fatal("ErrNoRelease must be set")
	}
	if !strings.Contains(ErrNoRelease.Error(), "no release found") {
		t.Fatalf("unexpected ErrNoRelease: %v", ErrNoRelease)
	}
}

func TestIsNewer(t *testing.T) {
	cases := []struct {
		cur, lat string
		want     bool
	}{
		{"dev", "v0.1.0", true},
		{"v0.4.0", "v0.5.0", true},
		{"v0.5.0", "v0.5.0", false},
		{"v0.5.1", "v0.5.0", false},
		{"0.5.0", "v0.6.0", true},
	}
	for _, c := range cases {
		if got := IsNewer(c.cur, c.lat); got != c.want {
			t.Errorf("IsNewer(%q,%q)=%v want %v", c.cur, c.lat, got, c.want)
		}
	}
}

func TestAssetForThisPlatform(t *testing.T) {
	suffix := "-" + runtime.GOOS + "-" + runtime.GOARCH
	if runtime.GOOS == "windows" {
		suffix += ".exe"
	}
	rel := &Release{Assets: []Asset{
		{Name: "SamNPlayer-gui" + suffix},
		{Name: "SamNPlayer-cli" + suffix},
		{Name: "SamNPlayer-suite-gui" + suffix},
		{Name: "other-file.txt"},
	}}
	gui, ok := rel.AssetForThisPlatform("gui")
	if !ok || gui.Name != "SamNPlayer-gui"+suffix {
		t.Fatalf("gui asset: %+v ok=%v", gui, ok)
	}
	cli, ok := rel.AssetForThisPlatform("cli")
	if !ok || cli.Name != "SamNPlayer-cli"+suffix {
		t.Fatalf("cli asset: %+v ok=%v", cli, ok)
	}
	suite, ok := rel.AssetForThisPlatform("suite-gui")
	if !ok || suite.Name != "SamNPlayer-suite-gui"+suffix {
		t.Fatalf("suite gui asset: %+v ok=%v", suite, ok)
	}
	// The suite asset is listed first in this order check: "gui" must
	// still pick the Free file, not the suite one that shares the tail.
	rel.Assets = []Asset{{Name: "SamNPlayer-suite-gui" + suffix}, {Name: "SamNPlayer-gui" + suffix}}
	if gui, _ := rel.AssetForThisPlatform("gui"); gui.Name != "SamNPlayer-gui"+suffix {
		t.Fatalf("gui picked %q, want the Free asset", gui.Name)
	}
	if _, ok := rel.AssetForThisPlatform("missing"); ok {
		t.Fatal("expected no asset for missing kind")
	}
}

func TestValidateGitHubAssetURL(t *testing.T) {
	good := "https://github.com/funfunpayer/SamNPlayer/releases/download/v0.5.0/SamNPlayer-gui-linux-amd64"
	if err := validateGitHubAssetURL(good); err != nil {
		t.Fatal(err)
	}
	if err := validateGitHubAssetURL("https://evil.example/x"); err == nil {
		t.Fatal("expected reject non-github host")
	}
}

func TestChangelogSummary(t *testing.T) {
	if ChangelogSummary("", 100) != "" {
		t.Fatal("empty body")
	}
	body := "## What's Changed\n* fix(gui): boxes by @x in https://example.com/1\n\n**Full Changelog**: https://example.com/c"
	got := ChangelogSummary(body, 200)
	if !strings.Contains(got, "fix(gui): boxes") {
		t.Fatalf("summary missing content: %q", got)
	}
	if strings.Contains(got, "##") {
		t.Fatalf("summary kept heading marks: %q", got)
	}
	long := strings.Repeat("word ", 200)
	short := ChangelogSummary(long, 40)
	if len([]rune(short)) > 40 {
		t.Fatalf("not truncated: %q", short)
	}
}

func TestWindowsApplyBatch(t *testing.T) {
	bat := windowsApplyBatch(4242, `C:\Temp\new.exe`, `C:\App\SamNPlayer.exe`)
	for _, want := range []string{
		"4242",
		`move /Y "C:\Temp\new.exe" "C:\App\SamNPlayer.exe"`,
		`start "" "C:\App\SamNPlayer.exe"`,
		`del "%~f0"`,
	} {
		if !strings.Contains(bat, want) {
			t.Fatalf("batch missing %q in:\n%s", want, bat)
		}
	}
	// Must not be a one-liner passed through cmd /C (quote-mangling bug).
	if strings.Count(bat, "\r\n") < 5 {
		t.Fatalf("expected multi-line .cmd, got:\n%s", bat)
	}
}

func TestReleaseBodyJSON(t *testing.T) {
	const raw = `{"tag_name":"v0.5.37","html_url":"https://github.com/funfunpayer/SamNPlayer/releases/tag/v0.5.37","body":"## Notes\n* hello","assets":[]}`
	var rel Release
	if err := json.Unmarshal([]byte(raw), &rel); err != nil {
		t.Fatal(err)
	}
	if rel.TagName != "v0.5.37" || rel.Body == "" || !strings.Contains(rel.Body, "hello") {
		t.Fatalf("unexpected release: %+v", rel)
	}
	if sum := ChangelogSummary(rel.Body, 80); !strings.Contains(sum, "hello") {
		t.Fatalf("summary=%q", sum)
	}
}
