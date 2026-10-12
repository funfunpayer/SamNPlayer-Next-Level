//go:build !opencv

package generator

import "testing"

func TestNativeTrackingUnavailableWithoutOpenCVTag(t *testing.T) {
	if NativeTrackingAvailable() {
		t.Fatal("default build must not enable native tracking without -tags opencv")
	}
}
