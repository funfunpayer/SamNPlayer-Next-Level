package funscript

import (
	"os"
	"path/filepath"
)

// writeFileAtomic writes data to path via a same-dir temp file + rename so a
// crash mid-write cannot leave a truncated .funscript (same pattern as
// generator native export and samn ExportFunscript).
func writeFileAtomic(path string, data []byte, mode os.FileMode) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), "."+filepath.Base(path)+".tmp-*")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	defer func() {
		_ = tmp.Close()
		_ = os.Remove(tmpName)
	}()

	if err := tmp.Chmod(mode); err != nil {
		return err
	}
	if _, err := tmp.Write(data); err != nil {
		return err
	}
	if err := tmp.Sync(); err != nil {
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmpName, path)
}

// WriteFileAtomic is writeFileAtomic for other packages (e.g. the GUI
// settings store, which writes on every knob change).
func WriteFileAtomic(path string, data []byte, mode os.FileMode) error {
	return writeFileAtomic(path, data, mode)
}
