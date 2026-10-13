//go:build suite

package main

import "testing"

func TestSuiteBuildFlag(t *testing.T) {
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	if !NewApp().IsSuiteBuild() {
		t.Fatal("-tags suite must report the suite edition")
	}
	if k := guiAssetKind(); k != "suite-gui" {
		t.Fatalf("suite build must update from the suite asset, got %q", k)
	}
}
