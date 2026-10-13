package funscript

import (
	"encoding/json"
	"fmt"
	"os"
)

// StampAudioCheck writes metadata.audio_check without rewriting actions.
func StampAudioCheck(path string, check *AudioCheck) error {
	if check == nil {
		return nil
	}
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
	checkJSON, err := json.Marshal(check)
	if err != nil {
		return err
	}
	meta["audio_check"] = checkJSON
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
