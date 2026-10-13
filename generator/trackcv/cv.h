//go:build cgo && opencv

// cv.h - minimaler C-Wrapper um genau die OpenCV-Funktionen, die
// track.go für einen CSRT-Tracking-Durchlauf braucht (siehe
// generate_funscript.py's track_roi(), dessen Verhalten dieses Paket
// nachbildet). Bewusst nicht über gocv: dessen contrib-Paket bündelt alle
// Contrib-Module (inkl. aruco/xfeatures2d) in einer einzigen cgo-Einheit -
// xfeatures2d ist im Ubuntu-Paket libopencv-contrib-dev nicht enthalten
// (Patentgründe), wodurch gocv dort gar nicht erst kompiliert, obwohl nur
// der Tracking-Teil gebraucht wird. Dieser Wrapper bindet nur, was
// tatsächlich verwendet wird.
#ifndef SAMNPLAYER_TRACKCV_H
#define SAMNPLAYER_TRACKCV_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VideoCapture_ *VideoCapture;
typedef struct VideoWriter_ *VideoWriter;
typedef struct Tracker_ *TrackerHandle;
typedef struct Mat_ *MatHandle;

// --- Video-E/A -------------------------------------------------------------

VideoCapture VideoCapture_Open(const char *path);
void VideoCapture_Close(VideoCapture v);
int VideoCapture_Read(VideoCapture v);      // liest den nächsten Frame in den internen Puffer
int VideoCapture_IsOpened(VideoCapture v);
double VideoCapture_Get(VideoCapture v, int propId);
void VideoCapture_Seek(VideoCapture v, int frameIndex);

// width/height/step beschreiben den rohen BGR-Puffer (3 Bytes/Pixel);
// step ist die Bytebreite einer Zeile (>= width*3, für ungerade Breiten
// ggf. gepolstert).
VideoWriter VideoWriter_Open(const char *path, double fps, int width, int height);
void VideoWriter_Close(VideoWriter w);
void VideoWriter_WriteBGR(VideoWriter w, const unsigned char *data, int width, int height, int step);

// --- Bildoperationen auf dem AKTUELLEN Capture-Frame ------------------------

// Liefert die Graustufenversion des zuletzt gelesenen Frames als neuen
// MatHandle (Aufrufer muss Mat_Close aufrufen).
MatHandle Frame_ToGray(VideoCapture v);
void Mat_Close(MatHandle m);
int Mat_Width(MatHandle m);
int Mat_Height(MatHandle m);

// --- Szenenschnitt-Erkennung (siehe detect_scene_cut/frame_signature) ------

// Histogramm-Korrelation zweier Graubilder (64 Bins, normalisiert) -
// entspricht cv2.compareHist(..., HISTCMP_CORREL).
double Gray_HistCorrelation(MatHandle a, MatHandle b);

// Mittlere absolute Differenz zweier auf 64x48 verkleinerter Graubilder -
// entspricht frame_signature() + np.mean(np.abs(diff)).
double Gray_SignatureDiff(MatHandle a, MatHandle b);

// --- Kamerakompensation (siehe estimate_camera_motion) ----------------------

// Schätzt die vertikale Kameraverschiebung zwischen zwei Graubildern,
// Hintergrundmerkmale außerhalb von (excludeX,Y,W,H) (mit 10px Polsterung).
// Gibt 0.0 zurück, wenn zu wenige verlässliche Punkte gefunden wurden -
// exakt dieselbe "lieber keine Korrektur als eine geratene"-Regel wie das
// Python-Original.
double EstimateCameraMotionY(MatHandle prevGray, MatHandle gray,
                              int exX, int exY, int exW, int exH);

// Wie EstimateCameraMotionY, aber mit einer Liste von Exclude-Boxen
// (tracked ROI + soft masks / SceneMap exclude marks). n==0 ⇒ keine Punch-outs.
double EstimateCameraMotionYExcludes(MatHandle prevGray, MatHandle gray,
                                      const int *exX, const int *exY,
                                      const int *exW, const int *exH, int n);

// --- Erscheinungsgedächtnis (siehe AppearanceMemory) ------------------------

// Extrahiert und verkleinert (downscale) einen Bildausschnitt als neuen
// MatHandle - für AppearanceMemory.remember()'s Vorlagenspeicher.
MatHandle Gray_ExtractTemplate(MatHandle gray, int x, int y, int w, int h, double downscale);

// Verkleinert ein ganzes Graubild (kein Ausschnitt) - für
// AppearanceMemory.reacquire()'s self._prepare(gray) auf dem Suchbild.
MatHandle Gray_Downscale(MatHandle gray, double downscale);

// Template-Matching (TM_CCOEFF_NORMED) von template in frame. Schreibt die
// gefundene Position (in frame-Koordinaten, VOR Rückskalierung) und den
// Score nach *outX/*outY/*outScore. Gibt 0 zurück, wenn das Template
// größer als der Suchrahmen ist (kein Versuch).
int Gray_MatchTemplate(MatHandle frame, MatHandle tmpl, int *outX, int *outY, double *outScore);

// --- Rhythmus-Gitter (siehe rhythm_grid.go) --------------------------------

// Dichter optischer Fluss (Farneback, auf 320px Breite verkleinert) zwischen
// zwei Graubildern, abzüglich des Median-Flusses (Kamerabewegung), gemittelt
// auf ein gw x gh Zellgitter. Schreibt gw*gh Werte (zeilenweise, in
// Originalpixeln pro Frame) nach outVx/outVy.
void Gray_FlowCells(MatHandle prevGray, MatHandle gray, int gw, int gh,
                    float *outVx, float *outVy);

// --- CSRT-Tracker ------------------------------------------------------------

TrackerHandle Tracker_Create(void);
void Tracker_Close(TrackerHandle t);
void Tracker_Init(TrackerHandle t, VideoCapture v, int x, int y, int w, int h);
int Tracker_Update(TrackerHandle t, VideoCapture v, int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
