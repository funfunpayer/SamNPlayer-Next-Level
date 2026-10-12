//go:build !opencv || !cgo

// Package trackcv is the Go CSRT tracker. This stub file is compiled when
// the binary is built without `-tags opencv` (the default), so a fresh
// clone without libopencv-dev still builds. The real implementation lives
// behind `//go:build cgo && opencv` — enable it in CI/release (Linux or
// Windows) with `-tags opencv` after installing system OpenCV / MinGW
// OpenCV. See docs/WINDOWS_OPENCV.md.
package trackcv
