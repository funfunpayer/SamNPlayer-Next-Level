// Package update prüft gegen die GitHub-Releases-API des Projekts auf eine
// neuere Version und kann sich selbst ersetzen (Download + Neustart).
//
// WICHTIG: Das funktioniert nur, wenn das Projekt tatsächlich auf GitHub
// liegt und dort Releases mit angehängten Binaries erstellt werden (z.B.
// über den mitgelieferten .github/workflows/release.yml - der baut bei
// jedem Versions-Tag automatisch Windows/Linux-Binaries und hängt sie an
// den Release an). RepoOwner/RepoName unten MÜSSEN auf das echte Repo
// zeigen, sonst findet CheckLatest schlicht nichts (kein Fallback, keine
// Annahme - lieber ein klarer Fehler als ein stiller Nicht-Fund).
package update

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/funfunpayer/SamNPlayer/logging"
)

// RepoOwner/RepoName: GitHub-Nutzer/Repo, aus dem Releases gezogen werden.
const (
	RepoOwner = "funfunpayer"
	RepoName  = "SamNPlayer"
)

// Version wird beim Release-Build per -ldflags "-X ...update.Version=v1.2.3"
// gesetzt (siehe .github/workflows/release.yml). Im lokalen Dev-Build bleibt
// es "dev" - CheckLatest meldet dann immer "Update verfügbar", was für
// lokale Builds korrekt ist (sie haben ja keine echte Versionsnummer).
var Version = "dev"

// BaseVersion ist die im Quelltext festgeschriebene Fassung (Datei VERSION
// im Projektwurzelverzeichnis). Sie dient als Anhaltspunkt, wenn kein
// Release-Build vorliegt - dann steht in Version nur "dev", was beim
// Einordnen eines Fehlerberichts nicht weiterhilft.
//
// Bewusst eine Konstante und keine Datei, die zur Laufzeit gelesen wird:
// die fertige .exe soll eine einzelne Datei bleiben.
const BaseVersion = "0.6.0"

// Describe liefert die Fassung für Anzeige und Fehlerberichte.
func Describe() string {
	if Version != "" && Version != "dev" {
		return Version
	}
	return BaseVersion + "-dev"
}

// Release ist die für uns relevante Teilmenge der GitHub-Release-API-Antwort.
type Release struct {
	TagName string  `json:"tag_name"`
	HTMLURL string  `json:"html_url"`
	Body    string  `json:"body,omitempty"` // release notes / changelog markdown
	Assets  []Asset `json:"assets"`
}

type Asset struct {
	Name               string `json:"name"`
	BrowserDownloadURL string `json:"browser_download_url"`
	Size               int64  `json:"size"`
}

// AssetForThisPlatform sucht das zum aktuellen GOOS/GOARCH passende Asset.
// Erwartet Namensschema wie im Release-Workflow erzeugt:
// SamNPlayer-gui-windows-amd64.exe, SamNPlayer-gui-linux-amd64.
// AssetForThisPlatform sucht das Release-Asset für die angegebene
// Programm-Variante ("gui", "cli", "suite-gui", "suite-cli") und die
// aktuelle Plattform.
// Wichtig: der Name muss die Variante enthalten, nicht nur OS/Arch - sonst
// matchen die GUI- und die CLI-Datei unter Windows beide (beide enden auf
// "-windows-amd64.exe") und es wäre Zufall/Reihenfolge-abhängig, welche
// gewinnt. Das hätte dazu führen können, dass sich die GUI beim Update
// versehentlich durch die CLI-Datei ersetzt.
func (r *Release) AssetForThisPlatform(kind string) (Asset, bool) {
	// Exact name, not a suffix: "SamNPlayer-suite-gui-…" also ends in
	// "-gui-…", and a Free build must never update itself to the suite
	// edition (or the other way round).
	name := fmt.Sprintf("SamNPlayer-%s-%s-%s", kind, runtime.GOOS, runtime.GOARCH)
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	for _, a := range r.Assets {
		if a.Name == name {
			return a, true
		}
	}
	return Asset{}, false
}

// ChecksumFor sucht die SHA256-Prüfsumme für ein Asset in der
// checksums.txt dieses Releases (falls vorhanden). Leerer String + nil
// bedeutet: keine checksums.txt im Release, kein Fehler.
func (r *Release) ChecksumFor(assetName string) (string, error) {
	return checksumForAsset(r, assetName)
}

// ErrNoRelease means the GitHub API returned 404 for /releases/latest
// (no published release yet, or the repo is inaccessible without auth).
// Callers that only care about "is an update available?" should treat this
// as "nothing newer" rather than a user-facing failure.
var ErrNoRelease = fmt.Errorf("update: no release found (repo %s/%s has none yet, or is not publicly readable)", RepoOwner, RepoName)

// CheckLatest fragt die GitHub-API nach dem neuesten Release. Liefert
// (nil, nil) wenn RepoOwner noch auf den Platzhalter zeigt - bewusst kein
// Netzwerk-Aufruf gegen ein Repo, das mit Sicherheit nicht existiert.
func CheckLatest() (*Release, error) {
	logging.Info("update: checking for new version", "current_version", Version, "repo", RepoOwner+"/"+RepoName)
	if RepoOwner == "TODO-github-username" {
		logging.Warn("update: RepoOwner is placeholder, check skipped")
		return nil, fmt.Errorf("update: RepoOwner is not set yet (placeholder in update/update.go)")
	}

	url := fmt.Sprintf("https://api.github.com/repos/%s/%s/releases/latest", RepoOwner, RepoName)
	client := &http.Client{Timeout: 10 * time.Second}
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "SamNPlayer-updater")

	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("update: GitHub request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusNotFound {
		// Empty release list / private-or-missing repo: not a hard failure for
		// callers that only care about "is there something newer?".
		return nil, ErrNoRelease
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("update: GitHub responded with HTTP %d", resp.StatusCode)
	}

	var rel Release
	if err := json.NewDecoder(resp.Body).Decode(&rel); err != nil {
		return nil, fmt.Errorf("update: response could not be read: %w", err)
	}
	// Defense: /releases/latest is "most recent non-prerelease by date".
	// Patch tags (patch-vX.Y.Z-pN) must stay prereleases; if one is
	// mistakenly published as a full release, refuse it here so the
	// normal Update path is not fed a non-version tag.
	if !isPlainVersionTag(rel.TagName) {
		logging.Warn("update: latest release tag is not a plain vX.Y.Z — ignored", "tag", rel.TagName)
		return nil, ErrNoRelease
	}
	logging.Info("update: latest release found", "tag", rel.TagName)
	return &rel, nil
}

// isPlainVersionTag reports whether tag looks like a normal release
// (v0.5.36 / 0.5.36), not a patch (patch-v0.5.36-p1) or other channel.
func isPlainVersionTag(tag string) bool {
	t := strings.TrimSpace(tag)
	t = strings.TrimPrefix(t, "v")
	if t == "" || strings.ContainsAny(t, "-+/") {
		return false
	}
	parts := strings.Split(t, ".")
	if len(parts) != 3 {
		return false
	}
	for _, p := range parts {
		if p == "" {
			return false
		}
		for _, c := range p {
			if c < '0' || c > '9' {
				return false
			}
		}
	}
	return true
}

// IsNewer vergleicht zwei "vX.Y.Z"-Versionsstrings numerisch (kein echtes
// SemVer mit Suffixen wie -rc1 - für dieses Projekt reicht die einfache
// Form). "dev" gilt immer als älter als jede echte Version.
func IsNewer(current, latest string) bool {
	if current == "dev" {
		return true
	}
	cur := parseVersion(current)
	lat := parseVersion(latest)
	for i := 0; i < 3; i++ {
		if lat[i] != cur[i] {
			return lat[i] > cur[i]
		}
	}
	return false
}

func parseVersion(v string) [3]int {
	v = strings.TrimPrefix(strings.TrimSpace(v), "v")
	parts := strings.SplitN(v, ".", 3)
	var out [3]int
	for i := 0; i < len(parts) && i < 3; i++ {
		n, _ := strconv.Atoi(parts[i])
		out[i] = n
	}
	return out
}

// Download lädt ein Asset in eine temporäre Datei und gibt deren Pfad
// zurück. Prüft dabei:
//  1. dass die URL tatsächlich von GitHub kommt (nicht irgendwo hin
//     umgeleitet wurde - relevant, weil dieses Programm die heruntergeladene
//     Datei später als sich selbst ausführt, siehe ApplyAndRestart),
//  2. optional die SHA256-Prüfsumme, falls expectedSHA256 nicht leer ist
//     (siehe CheckLatest/checksumForAsset - die stammt aus einer
//     checksums.txt, die der Release-Workflow mit veröffentlicht).
//
// Ohne Prüfsumme (leerer expectedSHA256, z.B. weil das Release keine
// checksums.txt hat) wird trotzdem heruntergeladen, aber das ist bewusst
// schwächer abgesichert - CheckLatest versucht immer, eine Prüfsumme zu
// finden.
func Download(a Asset, expectedSHA256 string) (string, error) {
	if err := validateGitHubAssetURL(a.BrowserDownloadURL); err != nil {
		return "", err
	}

	client := &http.Client{Timeout: 5 * time.Minute}
	req, err := http.NewRequest(http.MethodGet, a.BrowserDownloadURL, nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "SamNPlayer-updater")

	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("update: download failed: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("update: HTTP %d while downloading %s", resp.StatusCode, a.BrowserDownloadURL)
	}

	tmp, err := os.CreateTemp("", "SamNPlayer-update-*"+filepath.Ext(a.Name))
	if err != nil {
		return "", err
	}
	defer tmp.Close()

	hasher := sha256.New()
	// Downloadgröße begrenzen (Programm-Binaries sind hier immer < 500MB) -
	// eine kompromittierte oder fehlerhafte Antwort soll nicht unbegrenzt
	// Speicherplatz füllen können.
	const maxDownloadBytes = 500 * 1024 * 1024
	limited := io.LimitReader(resp.Body, maxDownloadBytes+1)
	written, err := io.Copy(io.MultiWriter(tmp, hasher), limited)
	if err != nil {
		os.Remove(tmp.Name())
		return "", fmt.Errorf("update: download could not be saved: %w", err)
	}
	if written > maxDownloadBytes {
		os.Remove(tmp.Name())
		return "", fmt.Errorf("update: downloaded file exceeds expected max size (%d MB) — aborted", maxDownloadBytes/1024/1024)
	}

	if expectedSHA256 != "" {
		actual := hex.EncodeToString(hasher.Sum(nil))
		if !strings.EqualFold(actual, expectedSHA256) {
			os.Remove(tmp.Name())
			logging.Error("update: checksum mismatch", "expected", expectedSHA256, "actual", actual)
			return "", fmt.Errorf("update: SHA256 checksum mismatch (expected %s, got %s) — download discarded, update NOT applied", expectedSHA256, actual)
		}
		logging.Info("update: checksum verified", "sha256", actual)
	} else {
		logging.Warn("update: no checksum available (release has no checksums.txt) — integrity not verified")
	}

	return tmp.Name(), nil
}

// validateGitHubAssetURL stellt sicher, dass eine Asset-URL tatsächlich von
// GitHub bedient wird, bevor sie heruntergeladen und (nach Prüfsummen-Check)
// als neue Programmversion ausgeführt wird. GitHub liefert Release-Assets
// entweder direkt von github.com oder über objects.githubusercontent.com aus.
func validateGitHubAssetURL(rawURL string) error {
	u, err := url.Parse(rawURL)
	if err != nil {
		return fmt.Errorf("update: invalid asset URL: %w", err)
	}
	if u.Scheme != "https" {
		return fmt.Errorf("update: asset URL does not use HTTPS (%s) — rejected", u.Scheme)
	}
	host := strings.ToLower(u.Hostname())
	allowed := host == "github.com" ||
		strings.HasSuffix(host, ".github.com") ||
		strings.HasSuffix(host, ".githubusercontent.com")
	if !allowed {
		return fmt.Errorf("update: asset URL does not point to github.com (%s) — rejected to avoid running a file from an unexpected source", host)
	}
	return nil
}

// checksumForAsset lädt checksums.txt aus dem Release (falls vorhanden) und
// sucht die Zeile für assetName. Format wie von "sha256sum" erzeugt:
// "<hex-hash>  <dateiname>" pro Zeile. Liefert ("", nil) wenn es keine
// checksums.txt im Release gibt - das ist kein Fehler, nur ein schwächer
// abgesichertes Release (z.B. weil der Workflow das noch nicht erzeugt).
func checksumForAsset(rel *Release, assetName string) (string, error) {
	var checksumsAsset *Asset
	for i := range rel.Assets {
		if rel.Assets[i].Name == "checksums.txt" {
			checksumsAsset = &rel.Assets[i]
			break
		}
	}
	if checksumsAsset == nil {
		return "", nil
	}
	if err := validateGitHubAssetURL(checksumsAsset.BrowserDownloadURL); err != nil {
		return "", err
	}

	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Get(checksumsAsset.BrowserDownloadURL)
	if err != nil {
		return "", fmt.Errorf("update: checksums.txt could not be loaded: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("update: checksums.txt HTTP %d", resp.StatusCode)
	}

	data, err := io.ReadAll(io.LimitReader(resp.Body, 1<<20)) // 1MB reicht für eine Textliste bei weitem
	if err != nil {
		return "", fmt.Errorf("update: checksums.txt could not be read: %w", err)
	}

	for _, line := range strings.Split(string(data), "\n") {
		fields := strings.Fields(line)
		if len(fields) != 2 {
			continue
		}
		hash, name := fields[0], strings.TrimPrefix(fields[1], "*")
		if name == assetName {
			return hash, nil
		}
	}
	return "", fmt.Errorf("update: no entry for %q in checksums.txt", assetName)
}

// ApplyAndRestart ersetzt die aktuell laufende Programmdatei durch die unter
// newPath heruntergeladene und startet sie neu. Eine laufende .exe kann sich
// unter Windows nicht selbst überschreiben, während sie läuft - deshalb wird
// ein kurzlebiger Hilfsprozess gestartet, der wartet, bis der aktuelle
// Prozess beendet ist, dann die Datei ersetzt und neu startet. Diese
// Funktion kehrt im Erfolgsfall NICHT zurück - sie beendet den Prozess
// (os.Exit), damit der Hilfsprozess die Datei freigeben kann.
func ApplyAndRestart(newPath string) error {
	logging.Info("update: applying update and restarting", "new_file", newPath)
	self, err := os.Executable()
	if err != nil {
		return fmt.Errorf("update: own executable path unknown: %w", err)
	}
	self, err = filepath.EvalSymlinks(self)
	if err != nil {
		return fmt.Errorf("update: own executable path not resolvable: %w", err)
	}

	if err := os.Chmod(newPath, 0755); err != nil {
		return fmt.Errorf("update: execute permission could not be set: %w", err)
	}

	if err := startApplyHelper(os.Getpid(), newPath, self); err != nil {
		return err
	}

	os.Exit(0)
	return nil // unreachable
}

// ChangelogSummary returns a short plain-text preview of release notes for the
// update UI. Empty when Body is missing. Keeps Patch/hooks free to use Body.
func ChangelogSummary(body string, maxRunes int) string {
	body = strings.TrimSpace(body)
	if body == "" {
		return ""
	}
	if maxRunes <= 0 {
		maxRunes = 280
	}
	// Strip common markdown noise for a one-line / short status preview.
	lines := strings.Split(body, "\n")
	var kept []string
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "<!--") {
			continue
		}
		line = strings.TrimLeft(line, "#*_>` ")
		if line == "" {
			continue
		}
		kept = append(kept, line)
		if len(kept) >= 4 {
			break
		}
	}
	out := strings.Join(kept, " · ")
	runes := []rune(out)
	if len(runes) > maxRunes {
		return string(runes[:maxRunes-1]) + "…"
	}
	return out
}
