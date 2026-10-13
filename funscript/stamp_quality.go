package funscript

import (
	"encoding/json"
	"fmt"
	"os"
)

// StampQuality writes metadata.quality_score/quality_passed/quality_warnings
// without rewriting actions or disturbing other metadata fields - same
// surgical-update pattern as StampAudioCheck. Used to refresh a script's
// quality metadata after an edit (e.g. Review -> Improve) changed the
// actions but left the original raw-Generate quality snapshot in place.
func StampQuality(path string, q ScriptQualityResult) error {
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
	scoreJSON, err := json.Marshal(q.Score)
	if err != nil {
		return err
	}
	meta["quality_score"] = scoreJSON
	passedJSON, err := json.Marshal(q.Passed)
	if err != nil {
		return err
	}
	meta["quality_passed"] = passedJSON
	warningsJSON, err := json.Marshal(q.Warnings)
	if err != nil {
		return err
	}
	meta["quality_warnings"] = warningsJSON
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
