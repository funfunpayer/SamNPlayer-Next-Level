package generator

import (
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"testing"
)

// Dieser Test fängt eine Fehlerklasse ab, die im Entwicklungsbaum
// grundsätzlich unsichtbar ist.
//
// Beim Entwickeln liegen alle Python-Module nebeneinander im Ordner
// generator/ und finden sich gegenseitig. In der fertigen .exe werden sie
// aber in ein Temp-Verzeichnis geschrieben - und was dort nicht landet,
// existiert für den Python-Prozess nicht.
//
// Genau das ist passiert: eingebettet waren vier Dateien, geschrieben
// wurden zwei. Die Folgen zeigten sich erst beim Anwender und sahen nach
// drei verschiedenen Fehlern aus:
//
//   - "--backend flow" scheiterte am fehlenden flow_backend
//   - das gelernte Qualitätsmodell wurde nie gefunden
//   - die Geräteprüfung lief still gar nicht, weil ihr Import in einem
//     try/except steht und lautlos durchfiel
//
// Der Test liest daher die tatsächlichen Importe aus dem Quelltext, statt
// eine gepflegte Liste zu vergleichen - eine solche Liste wäre genau das,
// was hier schon einmal vergessen wurde.
func TestAllImportedModulesAreEmbedded(t *testing.T) {
	localModules := map[string]bool{}
	entries, err := os.ReadDir(".")
	if err != nil {
		t.Fatalf("generator-Verzeichnis nicht lesbar: %v", err)
	}
	for _, entry := range entries {
		name := entry.Name()
		if strings.HasSuffix(name, ".py") && !strings.HasSuffix(name, "_test.py") {
			localModules[strings.TrimSuffix(name, ".py")] = true
		}
	}

	// Importe aus allen Nicht-Test-Modulen sammeln: auch ein Modul, das nur
	// von einem anderen Modul importiert wird, muss mitgeliefert werden.
	importRe := regexp.MustCompile(`(?m)^\s*(?:import|from)\s+([a-z_][a-z0-9_]*)`)
	needed := map[string]bool{}
	for module := range localModules {
		source, err := os.ReadFile(module + ".py")
		if err != nil {
			t.Fatalf("%s.py nicht lesbar: %v", module, err)
		}
		for _, match := range importRe.FindAllStringSubmatch(string(source), -1) {
			if localModules[match[1]] {
				needed[match[1]] = true
			}
		}
	}

	if len(needed) == 0 {
		t.Fatal("keine lokalen Importe gefunden - der Test prüft dann nichts")
	}

	dir, err := os.MkdirTemp("", "embed-check-*")
	if err != nil {
		t.Fatalf("Temp-Verzeichnis: %v", err)
	}
	defer os.RemoveAll(dir)

	scriptPath, err := writeScriptToTemp()
	if err != nil {
		t.Fatalf("writeScriptToTemp: %v", err)
	}
	defer cleanupScriptTemp(scriptPath)
	tempDir := filepath.Dir(scriptPath)

	var missing []string
	for module := range needed {
		if _, err := os.Stat(filepath.Join(tempDir, module+".py")); err != nil {
			missing = append(missing, module+".py")
		}
	}
	sort.Strings(missing)
	if len(missing) > 0 {
		t.Errorf("diese Module werden importiert, landen aber nicht im "+
			"Temp-Verzeichnis der fertigen .exe: %v", missing)
	}

	// Das Hauptskript selbst muss ebenfalls dort liegen.
	if _, err := os.Stat(scriptPath); err != nil {
		t.Errorf("generate_funscript.py fehlt im Temp-Verzeichnis: %v", err)
	}
	// requirements.txt wird für die Fehlermeldung bei fehlenden Paketen
	// gebraucht.
	if _, err := os.Stat(filepath.Join(tempDir, "requirements.txt")); err != nil {
		t.Errorf("requirements.txt fehlt im Temp-Verzeichnis: %v", err)
	}
}

// Testdateien gehören nicht in die Auslieferung - sie werden im Betrieb nie
// ausgeführt und blähen nur das Temp-Verzeichnis auf.
func TestTestFilesAreNotShipped(t *testing.T) {
	scriptPath, err := writeScriptToTemp()
	if err != nil {
		t.Fatalf("writeScriptToTemp: %v", err)
	}
	defer cleanupScriptTemp(scriptPath)

	entries, err := os.ReadDir(filepath.Dir(scriptPath))
	if err != nil {
		t.Fatalf("Temp-Verzeichnis nicht lesbar: %v", err)
	}
	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), "_test.py") {
			t.Errorf("Testdatei wurde mit ausgeliefert: %s", entry.Name())
		}
	}
}
