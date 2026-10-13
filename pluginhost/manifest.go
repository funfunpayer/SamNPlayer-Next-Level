package pluginhost

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// CurrentAPIVersion is the host ↔ pack handshake number.
// Packs with apiVersion outside [MinAPIVersion, CurrentAPIVersion] are rejected.
const (
	CurrentAPIVersion = 1
	MinAPIVersion     = 1
)

// ManifestFile is the required filename inside a drop-in plugin folder.
const ManifestFile = "samn-plugin.json"

// Manifest is the on-disk contract for a SamNPlayer plugin pack
// (drop-folder asset packs; Virtual Person product is cancelled / out of scope).
type Manifest struct {
	APIVersion   int           `json:"apiVersion"`
	ID           string        `json:"id"`
	Name         string        `json:"name"`
	Version      string        `json:"version"`
	HostMinStage string        `json:"hostMinStage,omitempty"`
	Description  string        `json:"description,omitempty"`
	Entry        ManifestEntry `json:"entry"`
}

// ManifestEntry describes how the host should treat the pack contents.
// H1 only loads asset packs (web + character files). In-process Go modules
// stay compile-time / later stages — no dynamic .so for end users.
type ManifestEntry struct {
	Kind        string `json:"kind"` // "asset_pack"
	WebRoot     string `json:"webRoot,omitempty"`
	AssetsRoot  string `json:"assetsRoot,omitempty"`
	VRMRelative string `json:"vrmRelative,omitempty"`
}

// Pack is a discovered plugin folder with a valid manifest.
type Pack struct {
	Root     string   `json:"root"`
	Manifest Manifest `json:"manifest"`
}

// LoadManifest reads and validates samn-plugin.json from dir.
func LoadManifest(dir string) (Manifest, error) {
	path := filepath.Join(dir, ManifestFile)
	raw, err := os.ReadFile(path)
	if err != nil {
		return Manifest{}, err
	}
	var m Manifest
	if err := json.Unmarshal(raw, &m); err != nil {
		return Manifest{}, fmt.Errorf("pluginhost: invalid %s: %w", ManifestFile, err)
	}
	if err := m.Validate(); err != nil {
		return Manifest{}, err
	}
	return m, nil
}

// Validate checks handshake fields.
func (m Manifest) Validate() error {
	if m.APIVersion < MinAPIVersion || m.APIVersion > CurrentAPIVersion {
		return fmt.Errorf("pluginhost: apiVersion %d unsupported (host supports %d–%d)",
			m.APIVersion, MinAPIVersion, CurrentAPIVersion)
	}
	id := strings.TrimSpace(m.ID)
	if id == "" {
		return fmt.Errorf("pluginhost: manifest id required")
	}
	if strings.TrimSpace(m.Name) == "" {
		return fmt.Errorf("pluginhost: manifest name required")
	}
	if strings.TrimSpace(m.Version) == "" {
		return fmt.Errorf("pluginhost: manifest version required")
	}
	kind := strings.TrimSpace(m.Entry.Kind)
	if kind == "" {
		kind = "asset_pack"
	}
	if kind != "asset_pack" {
		return fmt.Errorf("pluginhost: entry.kind %q unsupported (want asset_pack)", kind)
	}
	return nil
}

// DiscoverPacks scans root for immediate child directories that contain a
// valid samn-plugin.json. Missing root yields an empty list (not an error).
func DiscoverPacks(root string) ([]Pack, error) {
	if root == "" {
		return nil, nil
	}
	entries, err := os.ReadDir(root)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	var out []Pack
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		dir := filepath.Join(root, e.Name())
		m, err := LoadManifest(dir)
		if err != nil {
			continue
		}
		out = append(out, Pack{Root: dir, Manifest: m})
	}
	return out, nil
}

// FindPackByID returns the first discovered pack with the given id.
func FindPackByID(root, id string) (Pack, bool, error) {
	packs, err := DiscoverPacks(root)
	if err != nil {
		return Pack{}, false, err
	}
	for _, p := range packs {
		if p.Manifest.ID == id {
			return p, true, nil
		}
	}
	return Pack{}, false, nil
}

// InstallPackDir copies srcDir (must contain a valid manifest) into
// pluginsRoot/<safe-id>/. Replaces an existing folder with the same id.
func InstallPackDir(pluginsRoot, srcDir string) (Pack, error) {
	m, err := LoadManifest(srcDir)
	if err != nil {
		return Pack{}, err
	}
	if err := os.MkdirAll(pluginsRoot, 0o755); err != nil {
		return Pack{}, err
	}
	dest := filepath.Join(pluginsRoot, sanitizeID(m.ID))
	overlaps, err := pathsOverlap(srcDir, dest)
	if err != nil {
		return Pack{}, fmt.Errorf("pluginhost: compare source and destination: %w", err)
	}
	if overlaps {
		return Pack{}, fmt.Errorf("pluginhost: source %q overlaps installation destination %q", srcDir, dest)
	}
	// Different manifest IDs may sanitize to the same directory name
	// (for example "foo/bar" and "foo_bar"). Never let one pack silently
	// replace another pack merely because their sanitized IDs collide.
	if existing, err := LoadManifest(dest); err == nil {
		if existing.ID != m.ID {
			return Pack{}, fmt.Errorf("pluginhost: plugin id %q collides with installed plugin %q", m.ID, existing.ID)
		}
	} else if !os.IsNotExist(err) {
		return Pack{}, fmt.Errorf("pluginhost: inspect existing plugin %q: %w", dest, err)
	}
	if err := os.RemoveAll(dest); err != nil {
		return Pack{}, err
	}
	if err := copyDir(srcDir, dest); err != nil {
		_ = os.RemoveAll(dest)
		return Pack{}, err
	}
	return Pack{Root: dest, Manifest: m}, nil
}

func pathsOverlap(a, b string) (bool, error) {
	canonical := func(path string) (string, error) {
		abs, err := filepath.Abs(path)
		if err != nil {
			return "", err
		}
		if resolved, err := filepath.EvalSymlinks(abs); err == nil {
			return resolved, nil
		} else if !os.IsNotExist(err) {
			return "", err
		}
		parent, err := filepath.EvalSymlinks(filepath.Dir(abs))
		if err != nil {
			return "", err
		}
		return filepath.Join(parent, filepath.Base(abs)), nil
	}

	a, err := canonical(a)
	if err != nil {
		return false, err
	}
	b, err = canonical(b)
	if err != nil {
		return false, err
	}
	contains := func(parent, child string) (bool, error) {
		rel, err := filepath.Rel(parent, child)
		if err != nil {
			return false, err
		}
		return rel == "." || (rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator))), nil
	}
	if yes, err := contains(a, b); err != nil || yes {
		return yes, err
	}
	return contains(b, a)
}

func sanitizeID(id string) string {
	id = strings.TrimSpace(id)
	var b strings.Builder
	for _, r := range id {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9', r == '-', r == '_':
			b.WriteRune(r)
		default:
			b.WriteByte('_')
		}
	}
	out := b.String()
	if out == "" {
		return "plugin"
	}
	return out
}

func copyDir(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		target := filepath.Join(dst, rel)
		if info.IsDir() {
			return os.MkdirAll(target, 0o755)
		}
		if info.Mode()&os.ModeSymlink != 0 {
			return nil // skip symlinks for safety
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		return os.WriteFile(target, data, info.Mode().Perm())
	})
}
