//go:build !cgo || !opencv

package generator

import "errors"

// ScanSceneMap is unavailable without OpenCV (same gate as native CSRT).
func ScanSceneMap(videoPath string, n int) (SceneMapDTO, error) {
	return SceneMapDTO{}, errors.New("generator: ScanSceneMap requires -tags opencv + cgo + OpenCV")
}
