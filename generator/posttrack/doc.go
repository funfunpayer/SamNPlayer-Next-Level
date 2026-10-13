// Package posttrack is the pure-Go port of generate_funscript.py's
// post-tracking signal path: Savitzky-Golay smoothing, percentile
// normalisation, optional dynamic-range lift, peak/valley detection with
// prominence, adaptive keyframe densification, minimum action spacing, RDP
// simplification, and speed limiting.
//
// Measured against committed Python goldens (testdata/positions_goldens.json)
// generated from the same functions in generate_funscript.py — dense-curve
// agreement within 0.05 position units and byte-identical action lists on
// every fixture. No cgo, no OpenCV, no Python runtime.
//
// Combined with generator/trackcv this is the second step toward a CSRT
// generation path that does not need a Python install. Quality Doctor and
// audio-tempo check run in Go on the native path; AI opinion and non-CSRT
// backends stay on the Python path for now.
package posttrack
