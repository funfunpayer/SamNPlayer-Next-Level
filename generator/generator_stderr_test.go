package generator

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

// writeFakePythonChatty verhält sich wie writeFakePython (Abhängigkeits-
// prüfung besteht), schreibt beim eigentlichen Aufruf aber viele stderr-
// Zeilen und danach GENAU EINE stdout-Zeile ("ROI 1 2 3 4"), um
// FindROIWithProgress nachzustellen: viel stderr, wenig stdout, Prozess
// beendet sich sofort danach.
func writeFakePythonChatty(t *testing.T, dir string, lines int) string {
	t.Helper()
	// EIN printf mit Bash-Brace-Expansion statt einer Schleife mit vielen
	// einzelnen echo-Aufrufen: das schreibt die komplette stderr-Ausgabe in
	// einem Rutsch (wenige Millisekunden statt hunderter), bevor die
	// stdout-Zeile kommt und der Prozess sich beendet - genau das Zeitfenster,
	// das die Race in cmd.Wait() vs. der stderr-Goroutine ausnutzt. Mit
	// vielen einzelnen echo-Aufrufen (ursprünglicher Versuch) dauerte der
	// Kindprozess lang genug, dass die Goroutine immer problemlos mithielt -
	// die Race blieb unsichtbar, obwohl der Code-Pfad falsch war (siehe
	// os/exec-Doku). "seq" ist wegen withOnlyPath (PATH exklusiv auf das
	// Fake-Interpreter-Verzeichnis gesetzt) nicht nutzbar, Brace-Expansion
	// braucht kein externes Programm.
	// stdout ZUERST, dann der stderr-Schwall: die Haupt-Goroutine liest
	// stdout, bekommt seine eine Zeile praktisch sofort und will dann nur
	// noch auf das Prozessende warten - genau dort soll sie der
	// stderr-Goroutine keinen Vorsprung mehr lassen.
	body := "#!/bin/bash\n" +
		"if [ \"$1\" = \"-c\" ] && [ \"$2\" = \"pass\" ]; then exit 0; fi\n" +
		"echo \"ROI 1 2 3 4\"\n" +
		fmt.Sprintf("printf 'line %%d\\n' {1..%d} >&2\n", lines) +
		"exit 0\n"
	path := filepath.Join(dir, "python3")
	if err := os.WriteFile(path, []byte(body), 0755); err != nil {
		t.Fatalf("Fake-Interpreter anlegen: %v", err)
	}
	return path
}

// TestFindROIWithProgressDrainsAllStderr schützt gegen die in den os/exec-
// Docs beschriebene Falle: "it is thus incorrect to call Wait before all
// reads from the pipe have completed". findROIViaScript las stderr in einer
// eigenen Goroutine, rief cmd.Wait() aber auf, sobald die stdout-Pipe zu
// Ende war - nicht, sobald die stderr-Goroutine fertig war. Laut
// os/exec-Quellcode (Cmd.Wait ruft closeDescriptors(c.parentIOPipes)
// unmittelbar nach Prozessende auf) schließt das die stderr-Pipe, ohne auf
// eine selbst gestartete Lese-Goroutine zu warten - dieselbe Klasse Bug wie
// bereits in cmd/gui-wails/app_playback.go gefunden (siehe dortiger Fix).
//
// Ehrlicher Befund zu diesem Test: drei Varianten mit absichtlich engem
// Zeitfenster (viele stderr-Zeilen kurz vor Prozessende, stdout zuerst,
// GOMAXPROCS=1) haben den Datenverlust in dieser Umgebung NICHT zuverlässig
// ausgelöst - Gos Scheduler startet die stderr-Goroutine hier offenbar
// zuverlässig, bevor der (absichtlich sehr schnelle) Testprozess sich schon
// beendet hat. Die Fehlerhaftigkeit des Codepfads selbst ist trotzdem durch
// Dokumentation und Quellcode belegt, nicht nur vermutet - dieser Test
// bleibt als Vollständigkeits-/Regressionsschutz bestehen (alle
// stderr-Zeilen müssen ankommen), auch ohne dass er den Fehler hier selbst
// scharf stellen konnte.
func TestFindROIWithProgressDrainsAllStderr(t *testing.T) {
	dir := t.TempDir()
	withOnlyPath(t, dir)
	// Unter der üblichen Pipe-Puffergröße (64KB unter Linux) bleiben, damit
	// printf nicht blockiert und in einem Rutsch schreibt.
	const totalLines = 4000
	writeFakePythonChatty(t, dir, totalLines)

	for attempt := 0; attempt < 30; attempt++ {
		var mu sync.Mutex
		seen := 0
		_, err := FindROIWithProgress("dummy.mp4", func(line string) {
			if strings.HasPrefix(line, "line ") {
				mu.Lock()
				seen++
				mu.Unlock()
			}
		}, nil)
		if err != nil {
			t.Fatalf("Durchlauf %d: FindROIWithProgress: %v", attempt, err)
		}
		mu.Lock()
		got := seen
		mu.Unlock()
		if got != totalLines {
			t.Fatalf("Durchlauf %d: %d von %d stderr-Zeilen empfangen - "+
				"cmd.Wait() lief vor Abschluss des stderr-Lesens", attempt, got, totalLines)
		}
	}
}
