package update

import (
	"encoding/json"
	"fmt"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// Patch is a hotfix published for a specific base version (additive to
// full Releases). Tags look like "patch-v0.5.36-p1" and MUST be GitHub
// prereleases so they never become /releases/latest.
type Patch struct {
	ID          string  `json:"id"`           // e.g. "v0.5.36-p1"
	TagName     string  `json:"tag_name"`     // e.g. "patch-v0.5.36-p1"
	BaseVersion string  `json:"base_version"` // e.g. "0.5.36"
	PatchNumber int     `json:"patch_number"`
	HTMLURL     string  `json:"html_url"`
	Body        string  `json:"body,omitempty"`
	Assets      []Asset `json:"assets"`
}

// AssetForThisPlatform mirrors Release.AssetForThisPlatform.
func (p *Patch) AssetForThisPlatform(kind string) (Asset, bool) {
	rel := &Release{Assets: p.Assets}
	return rel.AssetForThisPlatform(kind)
}

// ChecksumFor mirrors Release.ChecksumFor.
func (p *Patch) ChecksumFor(assetName string) (string, error) {
	rel := &Release{Assets: p.Assets}
	return checksumForAsset(rel, assetName)
}

// patchTagRe matches patch-v0.5.36-p1 / patch-0.5.36-p2.
var patchTagRe = regexp.MustCompile(`(?i)^patch-v?(\d+\.\d+\.\d+)-p(\d+)$`)

// ParsePatchTag extracts base version and patch number from a release tag.
// ok is false when the tag is not a patch tag.
func ParsePatchTag(tag string) (base string, num int, id string, ok bool) {
	m := patchTagRe.FindStringSubmatch(strings.TrimSpace(tag))
	if m == nil {
		return "", 0, "", false
	}
	base = m[1]
	n, err := strconv.Atoi(m[2])
	if err != nil || n < 1 {
		return "", 0, "", false
	}
	id = "v" + base + "-p" + strconv.Itoa(n)
	return base, n, id, true
}

// NormalizeVersionStr strips a leading "v" and "-dev"/build junk for
// comparing a running build to a patch base.
func NormalizeVersionStr(v string) string {
	v = strings.TrimSpace(v)
	v = strings.TrimPrefix(v, "v")
	if i := strings.IndexAny(v, "-+"); i >= 0 {
		v = v[:i]
	}
	return v
}

// CurrentBaseVersion returns the base used for patch matching:
// release builds use Version; local/dev builds use BaseVersion.
func CurrentBaseVersion() string {
	if Version != "" && Version != "dev" {
		return NormalizeVersionStr(Version)
	}
	return NormalizeVersionStr(BaseVersion)
}

type ghReleaseLite struct {
	TagName    string  `json:"tag_name"`
	HTMLURL    string  `json:"html_url"`
	Body       string  `json:"body"`
	Prerelease bool    `json:"prerelease"`
	Draft      bool    `json:"draft"`
	Assets     []Asset `json:"assets"`
}

// CheckLatestPatch queries GitHub releases for the highest unapplied patch
// matching currentBase (e.g. "0.5.36"). Returns (nil, nil) when none.
// Network/API failures return an error (callers treat like Update check).
func CheckLatestPatch(currentBase string) (*Patch, error) {
	currentBase = NormalizeVersionStr(currentBase)
	if currentBase == "" {
		return nil, nil
	}
	logging.Info("update: checking for patches", "base", currentBase, "repo", RepoOwner+"/"+RepoName)
	if RepoOwner == "TODO-github-username" {
		return nil, fmt.Errorf("update: RepoOwner is not set yet (placeholder in update/update.go)")
	}

	list, err := listRecentReleases(30)
	if err != nil {
		return nil, err
	}

	applied, _ := AppliedPatchIDs() // empty on first run / IO error — still offer

	var best *Patch
	for _, r := range list {
		if r.Draft {
			continue
		}
		base, num, id, ok := ParsePatchTag(r.TagName)
		if !ok {
			continue
		}
		if NormalizeVersionStr(base) != currentBase {
			continue
		}
		if applied[id] {
			continue
		}
		cand := &Patch{
			ID:          id,
			TagName:     r.TagName,
			BaseVersion: base,
			PatchNumber: num,
			HTMLURL:     r.HTMLURL,
			Body:        r.Body,
			Assets:      r.Assets,
		}
		if best == nil || cand.PatchNumber > best.PatchNumber {
			best = cand
		}
	}
	if best != nil {
		logging.Info("update: patch available", "id", best.ID, "tag", best.TagName)
	} else {
		logging.Info("update: no applicable patch", "base", currentBase)
	}
	return best, nil
}

func listRecentReleases(perPage int) ([]ghReleaseLite, error) {
	if perPage <= 0 {
		perPage = 30
	}
	url := fmt.Sprintf("https://api.github.com/repos/%s/%s/releases?per_page=%d", RepoOwner, RepoName, perPage)
	client := &http.Client{Timeout: 15 * time.Second}
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "SamNPlayer-updater")

	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("update: GitHub releases list failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusNotFound {
		return nil, ErrNoRelease
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("update: GitHub releases list HTTP %d", resp.StatusCode)
	}

	var list []ghReleaseLite
	if err := json.NewDecoder(resp.Body).Decode(&list); err != nil {
		return nil, fmt.Errorf("update: releases list could not be read: %w", err)
	}
	return list, nil
}
