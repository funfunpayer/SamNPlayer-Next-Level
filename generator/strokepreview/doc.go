// Package strokepreview is Stage A of the owner “extrema + Abtastung” idea
// (docs/NEXT.md § Stroke preview):
//
//	sparse sample the video → estimate up-endpoints (peaks) and down-points
//	(valleys) from motion → flag hard cuts / camera pans → optionally hint
//	“run audio check” when the preview looks unstable.
//
// It is NOT a tracker and must NOT invent the 0–100 funscript curve.
// CSRT / region_fusion / Tf-Tj remain the position path. Preview output is
// timing + flags that later stages can use to re-anchor, bias peak distance,
// or gate audio (AUDIO_WORKFLOW: audio only when tracking looks bad).
package strokepreview
