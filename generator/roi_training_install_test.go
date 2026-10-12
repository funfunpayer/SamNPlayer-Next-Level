package generator

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// Model pip's shared-file failure: uninstalling plain OpenCV deletes cv2,
// but contrib metadata still says the latest version is installed. A normal
// upgrade succeeds without restoring files; only a reinstall repairs them.
// Exercise the public installer, including its final dependency checks.
func TestInstallRoiTrainingDepsRepairsSharedOpenCVFiles(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("fake Python uses a Unix shell; Windows smoke test still required")
	}
	for _, failRestore := range []bool{false, true} {
		name := "repair"
		if failRestore {
			name = "restore_failure"
		}
		t.Run(name, func(t *testing.T) {
			dir := t.TempDir()
			withOnlyPath(t, dir)
			t.Setenv("LOCALAPPDATA", "")
			t.Setenv("ProgramFiles", "")
			t.Setenv("SAMN_TEST_CV_STATE", filepath.Join(dir, "cv-state"))
			t.Setenv("SAMN_TEST_PIP_LOG", filepath.Join(dir, "pip-log"))
			failure := "0"
			if failRestore {
				failure = "1"
			}
			t.Setenv("SAMN_TEST_FAIL_RESTORE", failure)
			body := `#!/bin/bash
if [ "$1" = "-c" ]; then
  if [ "$2" = "pass" ]; then exit 0; fi
  if [ -f "$SAMN_TEST_CV_STATE" ]; then exit 0; fi
  echo "ModuleNotFoundError: No module named 'cv2'" >&2
  exit 1
fi
if [ "$1" = "-m" ] && [ "$2" = "pip" ]; then
  echo "$*" >> "$SAMN_TEST_PIP_LOG"
  case " $* " in
    *" --force-reinstall "*)
      if [ "$SAMN_TEST_FAIL_RESTORE" = "1" ]; then exit 1; fi
      echo restored > "$SAMN_TEST_CV_STATE"
      ;;
  esac
  exit 0
fi
if [ "$2" = "--check" ]; then echo AVAILABLE; exit 0; fi
exit 2
`
			if err := os.WriteFile(filepath.Join(dir, "python3"), []byte(body), 0755); err != nil {
				t.Fatal(err)
			}
			err := InstallRoiTrainingDeps(nil)
			if failRestore {
				if err == nil || !strings.Contains(err.Error(), "restore failed") {
					t.Fatalf("failed reinstall must be reported, got %v", err)
				}
			} else if err != nil {
				t.Fatalf("repair left bootstrap unavailable: %v", err)
			}
			data, err := os.ReadFile(os.Getenv("SAMN_TEST_PIP_LOG"))
			if err != nil {
				t.Fatal(err)
			}
			for _, line := range strings.Split(string(data), "\n") {
				if strings.Contains(line, "--force-reinstall") &&
					(!strings.Contains(line, "--no-deps") || strings.Contains(line, "scipy") || strings.Contains(line, "numpy")) {
					t.Fatalf("reinstall must be limited to the damaged OpenCV wheel: %s", line)
				}
			}
		})
	}
}
