package funscript

import (
	"encoding/json"
	"fmt"
	"os"
)

// StampTrackingGaps replaces metadata.tracking_gaps surgically (same pattern
// as StampQuality). Pass nil or empty to clear after HealTrackingGaps so
// Contact vibration is not muted on rewritten windows forever.
func StampTrackingGaps(path string, gaps []TrackingGap) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	meta := map[string]json.RawMessage{}
	if raw, ok := doc["metadata"]; ok && len(raw) > 0 && string(raw) != "null" {
		if err := json.Unmarshal(raw, &meta); err != nil {
			return fmt.Errorf("funscript: metadata ungültig: %w", err)
		}
	}
	if len(gaps) == 0 {
		delete(meta, "tracking_gaps")
	} else {
		gapsJSON, err := json.Marshal(gaps)
		if err != nil {
			return err
		}
		meta["tracking_gaps"] = gapsJSON
	}
	metaJSON, err := json.Marshal(meta)
	if err != nil {
		return err
	}
	doc["metadata"] = metaJSON
	out, err := json.Marshal(doc)
	if err != nil {
		return err
	}
	return writeFileAtomic(path, out, 0o644)
}
