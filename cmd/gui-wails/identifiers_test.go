package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Es soll kein Klarname und kein alter Projektname im Quelltext stehen.
// Der Grund ist nicht Kosmetik: Bezeichner wandern in die fertige .exe,
// in die Dateieigenschaften, in den User-Agent des Auto-Updaters und in das
// "creator"-Feld JEDER erzeugten .funscript-Datei - also in Dateien, die
// weitergegeben werden.
//
// Der Test läuft über den Quelltext, weil ein einzelner neuer Codeschnipsel
// genügt, um den Namen wieder einzuschleppen, und das sonst erst auffiele,
// wenn die Datei schon beim Empfänger liegt.
func TestNoPersonalIdentifiersInSource(t *testing.T) {
	forbidden := []string{
		"christian", // Klarname
		"funscript-player",
		"gui-wails.exe",
		"TODO-github",
	}

	skipDirs := map[string]bool{
		"node_modules": true, "dist": true, ".git": true,
		"build": true, "__pycache__": true,
	}
	checkExt := map[string]bool{
		".go": true, ".py": true, ".js": true, ".json": true,
		".yml": true, ".yaml": true, ".md": true, ".html": true, ".css": true,
	}

	root := "../.."
	var hits []string
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil // unlesbare Pfade überspringen statt abzubrechen
		}
		if info.IsDir() {
			if skipDirs[info.Name()] {
				return filepath.SkipDir
			}
			return nil
		}
		if !checkExt[strings.ToLower(filepath.Ext(path))] {
			return nil
		}
		// Diese Datei selbst enthält die verbotenen Begriffe naturgemäß.
		if strings.HasSuffix(path, "identifiers_test.go") {
			return nil
		}
		data, readErr := os.ReadFile(path)
		if readErr != nil {
			return nil
		}
		lower := strings.ToLower(string(data))
		for _, word := range forbidden {
			if strings.Contains(lower, word) {
				hits = append(hits, path+": "+word)
			}
		}
		return nil
	})
	if err != nil {
		t.Fatalf("Durchsuchen fehlgeschlagen: %v", err)
	}
	for _, hit := range hits {
		t.Errorf("verbotener Bezeichner gefunden in %s", hit)
	}
}
