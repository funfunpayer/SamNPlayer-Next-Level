package update

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// appliedPatchesFile is stored under the user config dir (same tree as
// plugins/settings): SamNPlayer/applied_patches.json
const appliedPatchesFile = "applied_patches.json"

type appliedPatchesDoc struct {
	BaseVersion string            `json:"base_version"`
	IDs         map[string]string `json:"ids"` // id -> RFC3339 applied-at
}

var (
	appliedMu   sync.Mutex
	appliedTest map[string]string // if non-nil, overrides disk (unit tests)
)

// SetAppliedPatchesForTest injects an in-memory applied set (tests only).
// Pass nil to restore normal disk-backed behaviour.
func SetAppliedPatchesForTest(ids map[string]string) {
	appliedMu.Lock()
	defer appliedMu.Unlock()
	if ids == nil {
		appliedTest = nil
		return
	}
	cp := make(map[string]string, len(ids))
	for k, v := range ids {
		cp[k] = v
	}
	appliedTest = cp
}

func appliedPatchesPath() (string, error) {
	base, err := os.UserConfigDir()
	if err != nil || base == "" {
		return "", err
	}
	dir := filepath.Join(base, "SamNPlayer")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	return filepath.Join(dir, appliedPatchesFile), nil
}

// AppliedPatchIDs returns the set of patch IDs already applied for the
// current base. When the stored base differs from CurrentBaseVersion(),
// the list is treated as empty (full update moved us to a new line).
func AppliedPatchIDs() (map[string]bool, error) {
	appliedMu.Lock()
	defer appliedMu.Unlock()

	out := map[string]bool{}
	if appliedTest != nil {
		for id := range appliedTest {
			out[id] = true
		}
		return out, nil
	}

	path, err := appliedPatchesPath()
	if err != nil {
		return out, err
	}
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return out, nil
		}
		return out, err
	}
	var doc appliedPatchesDoc
	if err := json.Unmarshal(data, &doc); err != nil {
		return out, err
	}
	cur := CurrentBaseVersion()
	if NormalizeVersionStr(doc.BaseVersion) != cur {
		return out, nil
	}
	for id := range doc.IDs {
		out[id] = true
	}
	return out, nil
}

// MarkPatchApplied records that patchID was successfully downloaded and
// is about to replace the running binary. Call before ApplyAndRestart.
func MarkPatchApplied(patchID string) error {
	if patchID == "" {
		return nil
	}
	appliedMu.Lock()
	defer appliedMu.Unlock()

	if appliedTest != nil {
		appliedTest[patchID] = time.Now().UTC().Format(time.RFC3339)
		return nil
	}

	path, err := appliedPatchesPath()
	if err != nil {
		return err
	}
	cur := CurrentBaseVersion()
	doc := appliedPatchesDoc{
		BaseVersion: cur,
		IDs:         map[string]string{},
	}
	if data, err := os.ReadFile(path); err == nil {
		var prev appliedPatchesDoc
		if json.Unmarshal(data, &prev) == nil && NormalizeVersionStr(prev.BaseVersion) == cur {
			for k, v := range prev.IDs {
				doc.IDs[k] = v
			}
		}
	}
	doc.IDs[patchID] = time.Now().UTC().Format(time.RFC3339)
	raw, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, raw, 0o644)
}
