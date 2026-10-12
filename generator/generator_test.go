package generator

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// Hintergrund: Windows legt in %LOCALAPPDATA%\Microsoft\WindowsApps
// Alias-Stubs für python.exe/python3.exe an, die standardmäßig im PATH
// stehen. Diese zeigen auf eine andere Installation als die, in der der
// Nutzer per pip seine Pakete installiert hat. Die frühere Erkennung nahm
// den ERSTEN Treffer im PATH und scheiterte dann mit
// "ModuleNotFoundError: No module named 'cv2'", obwohl die Pakete auf dem
// Rechner vorhanden waren - nur eben in einem anderen Python.
//
// Der Test baut diese Situation mit zwei Skripten nach: eines ohne Pakete
// (steht zuerst), eines mit.

func writeFakePython(t *testing.T, dir, name string, hasPackages bool) string {
	t.Helper()
	if runtime.GOOS == "windows" {
		t.Skip("Fake-Interpreter per Shell-Skript funktioniert nur auf unix-artigen Systemen")
	}
	body := "#!/bin/bash\n" +
		"if [ \"$1\" = \"-c\" ] && [ \"$2\" = \"pass\" ]; then exit 0; fi\n"
	if hasPackages {
		body += "exit 0\n"
	} else {
		body += "echo \"ModuleNotFoundError: No module named 'cv2'\" >&2\nexit 1\n"
	}
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(body), 0755); err != nil {
		t.Fatalf("Fake-Interpreter anlegen: %v", err)
	}
	return path
}

// withOnlyPath setzt PATH exklusiv auf dir, damit ein echtes System-Python
// das Ergebnis nicht verfälscht.
func withOnlyPath(t *testing.T, dir string) {
	t.Helper()
	old := os.Getenv("PATH")
	t.Cleanup(func() { os.Setenv("PATH", old) })
	os.Setenv("PATH", dir)
}

func TestFindPythonSkipsInterpreterWithoutPackages(t *testing.T) {
	dir := t.TempDir()
	withOnlyPath(t, dir)
	// "python3" wird zuerst geprüft und hat KEINE Pakete (der Alias-Stub),
	// "python" hat sie.
	writeFakePython(t, dir, "python3", false)
	want := writeFakePython(t, dir, "python", true)

	got, err := FindPython()
	if err != nil {
		t.Fatalf("FindPython: %v", err)
	}
	if got != want {
		t.Errorf("FindPython() = %q, erwartet %q (das Python MIT Paketen)", got, want)
	}
	if err := CheckDependencies(); err != nil {
		t.Errorf("CheckDependencies sollte bestehen, meldet aber: %v", err)
	}
}

func TestCheckDependenciesListsAllCandidates(t *testing.T) {
	dir := t.TempDir()
	withOnlyPath(t, dir)
	writeFakePython(t, dir, "python3", false)
	writeFakePython(t, dir, "python", false)

	err := CheckDependencies()
	if err == nil {
		t.Fatal("ohne Pakete muss CheckDependencies einen Fehler liefern")
	}
	msg := err.Error()
	// Beide geprüften Interpreter müssen genannt werden - sonst rät der
	// Nutzer wieder, in welches Python er installieren soll.
	for _, name := range []string{"python3", "python"} {
		if !strings.Contains(msg, filepath.Join(dir, name)) {
			t.Errorf("Meldung nennt %q nicht:\n%s", name, msg)
		}
	}
	if !strings.Contains(msg, "-m pip install") {
		t.Errorf("Meldung enthält keinen pip-Befehl:\n%s", msg)
	}
}

func TestFindPythonErrorWhenNoneRunnable(t *testing.T) {
	dir := t.TempDir()
	withOnlyPath(t, dir)

	if _, err := FindPython(); err == nil {
		t.Fatal("ohne jeden Interpreter muss FindPython einen Fehler liefern")
	}
}

func TestIsWindowsAppsPythonStub(t *testing.T) {
	cases := []struct {
		path string
		want bool
	}{
		{`C:\Users\User\AppData\Local\Microsoft\WindowsApps\python3.exe`, true},
		{`C:/Users/User/AppData/Local/Microsoft/WindowsApps/python.exe`, true},
		{`C:\Users\User\AppData\Local\Programs\Python\Python314\python.exe`, false},
		{`/usr/bin/python3`, false},
		{`C:\Windows\py.exe`, false},
	}
	for _, tc := range cases {
		if got := isWindowsAppsPythonStub(tc.path); got != tc.want {
			t.Errorf("isWindowsAppsPythonStub(%q)=%v want %v", tc.path, got, tc.want)
		}
	}
}

func TestDemoteWindowsAppsStubs(t *testing.T) {
	stub := `C:\Users\x\AppData\Local\Microsoft\WindowsApps\python3.exe`
	real := `C:\Users\x\AppData\Local\Programs\Python\Python314\python.exe`
	pyLauncher := `C:\Windows\py.exe`

	got := demoteWindowsAppsStubs([]string{stub, real, pyLauncher})
	want := []string{real, pyLauncher, stub}
	if len(got) != len(want) {
		t.Fatalf("len=%d want %d: %v", len(got), len(want), got)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("[%d]=%q want %q (full=%v)", i, got[i], want[i], got)
		}
	}

	onlyStubs := demoteWindowsAppsStubs([]string{stub})
	if len(onlyStubs) != 1 || onlyStubs[0] != stub {
		t.Errorf("only stubs should stay as-is, got %v", onlyStubs)
	}
}
