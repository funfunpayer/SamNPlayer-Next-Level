package samn

import (
	"encoding/json"
	"fmt"
	"os"
)

// Load reads and validates a .samn document.
func Load(path string) (*Document, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("samn: read: %w", err)
	}
	return Parse(data)
}

// Parse unmarshals JSON bytes.
func Parse(data []byte) (*Document, error) {
	var d Document
	if err := json.Unmarshal(data, &d); err != nil {
		return nil, fmt.Errorf("samn: invalid JSON: %w", err)
	}
	if err := d.Normalize(); err != nil {
		return nil, err
	}
	return &d, nil
}

// Save writes the document as indented JSON.
func Save(path string, d *Document) error {
	if d == nil {
		return fmt.Errorf("samn: nil document")
	}
	if err := d.Normalize(); err != nil {
		return err
	}
	data, err := json.MarshalIndent(d, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
