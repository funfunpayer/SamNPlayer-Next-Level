//go:build !cgo || !opencv

package generator

import "errors"

// NativeTrackingAvailable is false unless the binary was built with
// `-tags opencv` (and cgo + system OpenCV). See native_track_opencv.go.
func NativeTrackingAvailable() bool { return false }

var errNativeUnavailable = errors.New("generator: native CSRT tracking is not available in this build (needs -tags opencv + cgo + OpenCV)")

func nativeTrackROI(videoPath string, roi ROI, opts nativeTrackOptions, onPercent func(int)) (nativeTrackResult, error) {
	return nativeTrackResult{}, errNativeUnavailable
}

func nativeTrackTwoPoints(videoPath string, roi, roi2 ROI, opts nativeTrackOptions, onPercent func(int)) (nativeTrackResult, error) {
	return nativeTrackResult{}, errNativeUnavailable
}

func nativeTrackMultiPoints(videoPath string, tip ROI, partners []nativePartner, opts nativeTrackOptions, onPercent func(int)) (nativeTrackResult, error) {
	return nativeTrackResult{}, errNativeUnavailable
}
