package edition

import "testing"

func TestNameMatchesSuite(t *testing.T) {
	if (Name() == "suite") != Suite() {
		t.Fatalf("Name %q vs Suite %v", Name(), Suite())
	}
}
