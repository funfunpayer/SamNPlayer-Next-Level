package sam

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

// SidecarPath is the .sam next to a .funscript (same basename).
func SidecarPath(funscriptPath string) string {
	return strings.TrimSuffix(funscriptPath, filepath.Ext(funscriptPath)) + ".sam"
}

// WriteEnrichedSidecar schreibt ein angereichertes .sam neben dem Funscript.
// .funscript bleibt die Nutzer-/Interchange-Datei; .sam ist das SAM-Modell.
func WriteEnrichedSidecar(funscriptPath string, fs *funscript.Script) error {
	if fs == nil {
		return fmt.Errorf("sam: kein Funscript")
	}
	s := FromFunscriptEnriched(fs)
	s.Metadata.Source = "funscript-sidecar"
	return s.Save(SidecarPath(funscriptPath))
}

// LoadSidecarIfPresent lädt die .sam neben dem Funscript, oder nil wenn keine.
func LoadSidecarIfPresent(funscriptPath string) (*Script, error) {
	path := SidecarPath(funscriptPath)
	if _, err := os.Stat(path); err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	return Load(path)
}
