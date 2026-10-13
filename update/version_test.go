package update

import (
	"os"
	"regexp"
	"strings"
	"testing"
)

// TestSourceVersion checks the source checkout, not the ldflags-injected
// Version. The release workflow supplies its tag explicitly; ordinary tests
// must also work on untagged development commits.
func TestSourceVersion(t *testing.T) {
	data, err := os.ReadFile("../VERSION")
	if err != nil {
		t.Fatal(err)
	}
	version := strings.TrimSpace(string(data))
	if !regexp.MustCompile(`^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$`).MatchString(version) {
		t.Fatalf("VERSION must be X.Y.Z without a prefix, got %q", version)
	}
	if version != BaseVersion {
		t.Fatalf("VERSION %q differs from update.BaseVersion %q", version, BaseVersion)
	}
	if tag, supplied := os.LookupEnv("SAMNPLAYER_RELEASE_TAG"); supplied && tag != "v"+version {
		t.Fatalf("release tag %q must match source version v%s", tag, version)
	}
}
