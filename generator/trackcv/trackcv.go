//go:build cgo && opencv

// Package trackcv bindet genau den Ausschnitt von OpenCV per cgo an, den
// track.go für eine Go-native Entsprechung von generate_funscript.py's
// track_roi() braucht. Siehe cv.h für die Begründung, warum das ein
// eigener schlanker Wrapper ist statt gocv.
package trackcv

/*
#cgo !windows pkg-config: opencv4
#cgo windows pkg-config: opencv5
#cgo windows CXXFLAGS: --std=c++17 -DNDEBUG
#include "cv.h"
#include <stdlib.h>
*/
import "C"
import "unsafe"

const (
	CapPropFrameWidth  = 3
	CapPropFrameHeight = 4
	CapPropFPS         = 5
	CapPropFrameCount  = 7
)

// VideoCapture liest ein Video Frame für Frame - der jeweils zuletzt
// gelesene Frame lebt intern in OpenCV (kein Go-seitiger Zugriff auf die
// Pixel nötig, alle Operationen laufen über die Wrapper-Funktionen unten).
type VideoCapture struct{ h C.VideoCapture }

func OpenVideo(path string) *VideoCapture {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	return &VideoCapture{h: C.VideoCapture_Open(cpath)}
}

func (v *VideoCapture) Close() { C.VideoCapture_Close(v.h) }
func (v *VideoCapture) IsOpened() bool {
	return C.VideoCapture_IsOpened(v.h) != 0
}
func (v *VideoCapture) Read() bool { return C.VideoCapture_Read(v.h) != 0 }
func (v *VideoCapture) Get(propID int) float64 {
	return float64(C.VideoCapture_Get(v.h, C.int(propID)))
}
func (v *VideoCapture) Seek(frameIndex int) {
	C.VideoCapture_Seek(v.h, C.int(frameIndex))
}

// Gray ist ein Graustufenbild - entweder der aktuelle Capture-Frame
// (ToGray) oder ein gemerkter Bildausschnitt (ExtractTemplate).
type Gray struct{ h C.MatHandle }

func (v *VideoCapture) ToGray() *Gray {
	return &Gray{h: C.Frame_ToGray(v.h)}
}

func (g *Gray) Close() {
	if g == nil || g.h == nil {
		return
	}
	C.Mat_Close(g.h)
}

func (g *Gray) Width() int  { return int(C.Mat_Width(g.h)) }
func (g *Gray) Height() int { return int(C.Mat_Height(g.h)) }

// HistCorrelation entspricht cv2.compareHist(..., HISTCMP_CORREL) auf
// 64-Bin-Histogrammen - siehe detect_scene_cut().
func HistCorrelation(a, b *Gray) float64 {
	return float64(C.Gray_HistCorrelation(a.h, b.h))
}

// SignatureDiff entspricht frame_signature() + np.mean(np.abs(diff)) -
// siehe detect_scene_cut().
func SignatureDiff(a, b *Gray) float64 {
	return float64(C.Gray_SignatureDiff(a.h, b.h))
}

// EstimateCameraMotionY entspricht estimate_camera_motion(): geschätzte
// vertikale Kameraverschiebung zwischen zwei Graubildern, aus
// Hintergrundmerkmalen außerhalb von exclude. 0.0 bedeutet "keine
// verlässliche Schätzung", nicht "keine Bewegung" - siehe cv.h.
func EstimateCameraMotionY(prev, gray *Gray, exclude Rect) float64 {
	return EstimateCameraMotionYExcludes(prev, gray, []Rect{exclude})
}

// EstimateCameraMotionYExcludes punches out every rect in excludes (tracked
// box + soft masks / SceneMap exclude marks) before sparse LK + RANSAC,
// matching Python estimate_camera_motion(..., extra_excludes=...).
func EstimateCameraMotionYExcludes(prev, gray *Gray, excludes []Rect) float64 {
	n := len(excludes)
	if n == 0 {
		return float64(C.EstimateCameraMotionYExcludes(prev.h, gray.h, nil, nil, nil, nil, 0))
	}
	xs := make([]C.int, n)
	ys := make([]C.int, n)
	ws := make([]C.int, n)
	hs := make([]C.int, n)
	for i, r := range excludes {
		xs[i], ys[i], ws[i], hs[i] = C.int(r.X), C.int(r.Y), C.int(r.W), C.int(r.H)
	}
	return float64(C.EstimateCameraMotionYExcludes(prev.h, gray.h,
		&xs[0], &ys[0], &ws[0], &hs[0], C.int(n)))
}

// FlowCells liefert die vertikale/horizontale Bewegung pro Gitterzelle
// zwischen prev und gray (Farneback-Fluss minus Median = Kamerabewegung) in
// Originalpixeln pro Frame, zeilenweise gw*gh Werte - Rohdaten fürs
// Rhythmus-Gitter (rhythm_grid.go).
func FlowCells(prev, gray *Gray, gw, gh int) (vx, vy []float32) {
	vx = make([]float32, gw*gh)
	vy = make([]float32, gw*gh)
	C.Gray_FlowCells(prev.h, gray.h, C.int(gw), C.int(gh),
		(*C.float)(unsafe.Pointer(&vx[0])), (*C.float)(unsafe.Pointer(&vy[0])))
	return vx, vy
}

// ExtractTemplate entspricht AppearanceMemory._prepare()+Ausschnitt -
// nil, wenn der Ausschnitt zu klein ist (Rand des Bilds, degenerierte Box).
func (g *Gray) ExtractTemplate(box Rect, downscale float64) *Gray {
	h := C.Gray_ExtractTemplate(g.h, C.int(box.X), C.int(box.Y), C.int(box.W), C.int(box.H), C.double(downscale))
	if h == nil {
		return nil
	}
	return &Gray{h: h}
}

// Downscale entspricht AppearanceMemory._prepare() auf dem GANZEN Bild
// (nicht auf einem Ausschnitt wie ExtractTemplate) - für reacquire()'s
// Suchbild.
func (g *Gray) Downscale(factor float64) *Gray {
	return &Gray{h: C.Gray_Downscale(g.h, C.double(factor))}
}

// MatchTemplate entspricht cv2.matchTemplate(..., TM_CCOEFF_NORMED) +
// minMaxLoc - liefert die beste Fundstelle (in frame-Koordinaten, VOR
// Rückskalierung) und deren Score. ok=false, wenn tmpl nicht in frame passt.
func MatchTemplate(frame, tmpl *Gray) (x, y int, score float64, ok bool) {
	var cx, cy C.int
	var cscore C.double
	r := C.Gray_MatchTemplate(frame.h, tmpl.h, &cx, &cy, &cscore)
	return int(cx), int(cy), float64(cscore), r != 0
}

// Rect ist eine Pixel-Bounding-Box, x/y oben links - entspricht dem
// (x, y, w, h)-Tupel, das die Python-Seite überall verwendet.
// Tracker ist ein CSRT-Tracker (cv::TrackerCSRT) - dieselbe Implementierung
// wie generate_funscript.py's create_tracker() letztlich aufruft, hier
// direkt über die moderne cv::Tracker-Schnittstelle ohne die drei
// API-Formen, die Python wegen OpenCV-Versionsunterschieden abfangen muss
// (siehe dortiger Kommentar) - dieses Paket bindet an eine feste,
// bekannte OpenCV-Version, das Problem entsteht hier gar nicht erst.
type Tracker struct{ h C.TrackerHandle }

func NewTracker() *Tracker { return &Tracker{h: C.Tracker_Create()} }
func (t *Tracker) Close()  { C.Tracker_Close(t.h) }

func (t *Tracker) Init(v *VideoCapture, box Rect) {
	C.Tracker_Init(t.h, v.h, C.int(box.X), C.int(box.Y), C.int(box.W), C.int(box.H))
}

func (t *Tracker) Update(v *VideoCapture) (box Rect, ok bool) {
	var x, y, w, h C.int
	r := C.Tracker_Update(t.h, v.h, &x, &y, &w, &h)
	return Rect{X: int(x), Y: int(y), W: int(w), H: int(h)}, r != 0
}

// VideoWriter schreibt ein BGR-Testvideo - nur für die Tests dieses Pakets
// gedacht (synthetische Videos mit bekannter Ground Truth, ohne dafür eine
// externe Abhängigkeit wie ffmpeg oder Python zu brauchen).
type VideoWriter struct{ h C.VideoWriter }

func OpenVideoWriter(path string, fps float64, width, height int) *VideoWriter {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	return &VideoWriter{h: C.VideoWriter_Open(cpath, C.double(fps), C.int(width), C.int(height))}
}

func (w *VideoWriter) Close() { C.VideoWriter_Close(w.h) }

// WriteBGR schreibt einen Frame aus einem rohen BGR-Puffer (3 Bytes je
// Pixel, zeilenweise, step = Bytes je Zeile).
func (w *VideoWriter) WriteBGR(data []byte, width, height, step int) {
	if len(data) == 0 {
		return
	}
	C.VideoWriter_WriteBGR(w.h, (*C.uchar)(unsafe.Pointer(&data[0])), C.int(width), C.int(height), C.int(step))
}
