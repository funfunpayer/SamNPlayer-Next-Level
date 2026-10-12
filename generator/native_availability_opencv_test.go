//go:build opencv

package generator

import "testing"

func TestNativeTrackingAvailableWithOpenCVTag(t *testing.T) {
	if !NativeTrackingAvailable() {
		t.Fatal("-tags opencv build must enable native tracking")
	}
}
