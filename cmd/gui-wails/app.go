package main

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	goruntime "runtime"
	"strings"
	"sync"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/logging"
	"github.com/funfunpayer/SamNPlayer/player"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// App ist der zentrale Zustand hinter der Wails-Bindung. Alle exportierten
// Methoden (Großbuchstabe) sind automatisch aus dem Frontend per
// window.go.main.App.<Methode>(...) aufrufbar.
type App struct {
	ctx      context.Context
	settings *settingsStore

	// Wiedergabe-Zustand
	currentScript *funscript.Script
	currentFrames []funscript.Frame
	scriptPath    string

	stateMu         sync.RWMutex
	videoPath       string
	videoURLEpoch   uint64 // bumps whenever videoPath changes so <video src> reloads
	videoServerPort int
	// Training stage film (app_training_video.go); separate from videoPath.
	trainingVideoPath  string
	trainingVideoEpoch uint64
	sessionActive      bool // true solange eine Wiedergabe ODER ein Training läuft

	activePlayer *player.Player
	// Per-channel output cut (V1 Inspector → Devices); zero value = full.
	// Guarded by stateMu; applied to every new and the running player.
	vibCut, sucCut float64
	peakReduce     bool
	// Per-motor levels (channel id → 0–1) for multi-motor devices; applied
	// to the connected device and again after every connect.
	motorLevels map[string]float64
	// Stroke output for strokers (D3), zero value = on, full range.
	strokeOff             bool
	strokeLo, strokeHiCut float64
	activeDevice          device.Device
	playCancel            context.CancelFunc
	videoPositionCh       chan int64

	// Dauerhafte Verbindung aus dem Geräte-Tab, bewusst getrennt von
	// activeDevice: activeDevice gehört einer laufenden Session und wird mit
	// ihr verworfen. Beides gleichzeitig ist nicht möglich (BLE erlaubt nur
	// eine Verbindung) und wird in app_device.go explizit abgelehnt.
	testDevice     device.Device
	testDeviceMock bool
	// testDeviceTransport: "ble" | "intiface" | "mock" — für die Fähigkeitsanzeige.
	testDeviceTransport string
	// testDeviceConnectLatencyMs: wie lange der letzte erfolgreiche
	// Connect() gedauert hat - fließt als erster Eintrag in den
	// Geräte-Diagnoselauf ein (app_diagnostics.go), statt die Verbindung
	// dafür extra zu trennen und neu aufzubauen.
	testDeviceConnectLatencyMs float64
	// testDeviceConnecting reserviert die Testverbindung, solange ein
	// ConnectDevice/ConnectDeviceVia-Aufruf noch läuft (siehe
	// claimTestDeviceConnect in app_device.go) - testDevice selbst bleibt
	// bis zum Erfolg nil.
	testDeviceConnecting bool

	// trainingControl erlaubt es, den laufenden Trainingszyklus zu
	// unterbrechen, ohne die Session zu beenden.
	trainingControl *player.TrainingControl

	// genCancel / genSeq: abort in-flight GenerateWithContext. genSeq
	// identifies the owner so a finished run does not clear a newer cancel.
	genCancel context.CancelFunc
	genSeq    uint64
	// roiSeq stamps AutoDetectROI results so a late find for video A cannot
	// paint the tip onto video B after a quick switch. roiCancel stops the
	// cancellable strict ONNX subprocess when a request is superseded.
	roiSeq    uint64
	roiCancel context.CancelFunc

	// benchmarkMu/benchmarkCancel: laufender Golden-Clip-Benchmark (siehe
	// app_benchmark.go) - eigener Lock, unabhängig von stateMu.
	benchmarkMu     sync.Mutex
	benchmarkCancel context.CancelFunc

	// audioVib: letzte Audio-Hüllkurve für "Vibration nur aus Audio"
	// (siehe app_audio_vibration.go) - eigener Lock.
	audioVib audioVibCache

	// scriptOffsetMs verschiebt das Skript gegen das Video. Pro Skript
	// gespeichert, weil er am Videoschnitt hängt und nicht an einer
	// allgemeinen Vorliebe.
	scriptOffsetMs    int64
	currentScriptPath string
}

func NewApp() *App {
	s, err := newSettingsStore()
	if err != nil {
		logging.Warn("app: settings could not be loaded", "error", err)
		s = &settingsStore{data: map[string]any{}}
	}
	return &App{settings: s}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	a.registerLogLiveHook()
	// Go already schedules on all CPUs; log so support/logs show the machine
	// budget. Do not force extra tracker parallelism — measured no net gain.
	logging.Info("gui-wails started",
		"goos", goruntime.GOOS,
		"goarch", goruntime.GOARCH,
		"num_cpu", goruntime.NumCPU(),
		"gomaxprocs", goruntime.GOMAXPROCS(0),
	)
	a.ensureRuntimeReady()
	a.registerFileDrop()
}

// --- Datei-Dialoge ---

func (a *App) PickFunscriptFile() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title:   "Choose script",
		Filters: scriptFileFilters(),
	})
}

func (a *App) PickVideoFile() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose video",
		Filters: []runtime.FileFilter{
			{DisplayName: "Videos", Pattern: "*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.m4v;*.webm;*.ts;*.m2ts;*.flv;*.mpg;*.mpeg;*.3gp;*.ogv"},
		},
	})
}

// PickBenchmarkManifest wählt die Manifest-Datei des Golden-Clip-Benchmarks
// (siehe generator/golden_clip_benchmark.py) - persönliches Nutzermaterial,
// darum eine Dateiauswahl statt eines im Repository mitgelieferten Pfads.
func (a *App) PickBenchmarkManifest() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose golden-clip manifest",
		Filters: []runtime.FileFilter{
			{DisplayName: "Manifest (*.json)", Pattern: "*.json"},
		},
	})
}

// PickContactPointsFile chooses a contact_points.py JSON for VLM1 rhythm-grid
// steering (Options.ContactPointsFile). Opt-in only; empty = off.
func (a *App) PickContactPointsFile() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose contact points JSON",
		Filters: []runtime.FileFilter{
			{DisplayName: "Contact points (*.json)", Pattern: "*.json;*.contact.json"},
		},
	})
}

// PickSceneProposalsFile chooses a scene_roles.py <clip>.scene.json for the
// Create pick-primary flow (Scene2). Opt-in only; user applies proposals.
func (a *App) PickSceneProposalsFile() (string, error) {
	return runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "Choose scene proposals JSON",
		Filters: []runtime.FileFilter{
			{DisplayName: "Scene proposals (*.scene.json)", Pattern: "*.scene.json;*.json"},
		},
	})
}

// --- Video-Auto-Match (dieselbe Logik wie vorher in der Fyne-GUI) ---

func findMatchingVideo(scriptPath string) (string, bool) {
	dir := filepath.Dir(scriptPath)
	base := strings.TrimSuffix(filepath.Base(scriptPath), filepath.Ext(scriptPath))
	for _, ext := range []string{".mp4", ".mkv", ".avi", ".mov", ".wmv", ".m4v", ".webm", ".ts", ".m2ts", ".flv", ".mpg", ".mpeg", ".3gp", ".ogv"} {
		candidate := filepath.Join(dir, base+ext)
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, true
		}
	}
	return "", false
}

// ScriptInfo wird als JSON ans Frontend zurückgegeben.
type ScriptInfo struct {
	Path                  string                  `json:"path"`
	ActionCount           int                     `json:"actionCount"`
	DurationMs            int64                   `json:"durationMs"`
	VideoPath             string                  `json:"videoPath"`
	HasVideo              bool                    `json:"hasVideo"`
	Profile               string                  `json:"profile"`
	ContactVibration      bool                    `json:"contactVibration"`
	ContactVibrationSpan  float64                 `json:"contactVibrationSpan"`
	ContactVibrationCurve string                  `json:"contactVibrationCurve"`
	NativeFormat          bool                    `json:"nativeFormat"`
	PlaybackSource        string                  `json:"playbackSource"`
	HasNeoAxes            bool                    `json:"hasNeoAxes"`
	HasContactMarks       bool                    `json:"hasContactMarks"`
	ContactMarks          *funscript.ContactMarks `json:"contactMarks,omitempty"`
	// Speech-Hold / Feel taxonomy from metadata.audio_check (review hints only).
	SpeechHoldMs  int64                        `json:"speechHoldMs,omitempty"`
	AudioSegments []funscript.AudioSegmentHint `json:"audioSegments,omitempty"`
	// AudioHz from metadata.audio_check (tempo hint for Play BPM grid; display only).
	AudioHz float64 `json:"audioHz,omitempty"`
}

func (a *App) LoadFunscript(path string) (ScriptInfo, error) {
	path = preferSamnCompanion(path)
	script, err := a.loadScriptDocument(path)
	if err != nil {
		return ScriptInfo{}, err
	}
	a.setLoadedScript(path, script)

	info := ScriptInfo{
		Path:           path,
		ActionCount:    len(script.Actions),
		DurationMs:     script.Duration(),
		Profile:        script.Metadata.Profile,
		NativeFormat:   samn.IsSamnPath(path),
		PlaybackSource: funscript.PlaybackSourceRecipe,
	}
	if dr := script.Metadata.DeviceRecipe; dr != nil {
		info.ContactVibration = dr.ContactVibration
		info.ContactVibrationSpan = funscript.EffectiveContactSpan(dr.ContactVibrationSpan)
		info.ContactVibrationCurve = funscript.NormalizeContactCurve(dr.ContactVibrationCurve)
		info.PlaybackSource = funscript.NormalizePlaybackSource(dr.PlaybackSource)
	}
	if ax := script.Metadata.SamnAxes; ax != nil {
		info.HasNeoAxes = ax.HasAxes()
	}
	if cm := script.Metadata.ContactMarks; cm != nil {
		info.ContactMarks = cm
		info.HasContactMarks = cm.HasAreas() || cm.TipClass != ""
	}
	fillScriptInfoAudioCheck(&info, script, path)
	a.stateMu.Lock()
	if video, ok := findMatchingVideo(path); ok {
		a.setVideoPathLocked(video)
		info.VideoPath = video
		info.HasVideo = true
	} else {
		a.setVideoPathLocked("")
	}
	a.stateMu.Unlock()
	return info, nil
}

// fillScriptInfoAudioCheck copies Speech-Hold / segment taxonomy into ScriptInfo.
// .samn ToFunscript does not carry audio_check; Improve stamps the companion
// .funscript — so when loading .samn we also peek that sidecar.
func fillScriptInfoAudioCheck(info *ScriptInfo, script *funscript.Script, path string) {
	if info == nil {
		return
	}
	apply := func(ac *funscript.AudioCheck) {
		if ac == nil {
			return
		}
		info.SpeechHoldMs = ac.SpeechHoldMs
		if ac.AudioHz != nil && *ac.AudioHz > 0 {
			info.AudioHz = *ac.AudioHz
		}
		if len(ac.Segments) > 0 {
			info.AudioSegments = append([]funscript.AudioSegmentHint(nil), ac.Segments...)
		}
	}
	if script != nil && script.Metadata.AudioCheck != nil {
		apply(script.Metadata.AudioCheck)
		return
	}
	if !samn.IsSamnPath(path) {
		return
	}
	funPath := samn.CompanionFunscriptPath(path)
	fs, err := funscript.Load(funPath)
	if err != nil || fs == nil || fs.Metadata.AudioCheck == nil {
		return
	}
	apply(fs.Metadata.AudioCheck)
}

// fileURL wandelt einen lokalen Pfad in eine file://-URL um - für Dinge wie
// den Log-Ordner (per OpenLogFolder/BrowserOpenURL geöffnet vom System, kein
// Cross-Origin-Problem). NICHT für <video src="...">  geeignet, siehe
// VideoFileURL für den Grund.
func fileURL(path string) string {
	return "file://" + filepath.ToSlash(path)
}

// setVideoPathLocked updates videoPath and bumps videoURLEpoch so the
// frontend <video> element gets a distinct src when the user swaps films.
// Caller must hold stateMu.
func (a *App) setVideoPathLocked(path string) {
	if a.videoPath == path {
		return
	}
	a.videoPath = path
	a.videoURLEpoch++
}

// VideoFileURL liefert die URL für das <video>-Element im Frontend.
//
// WICHTIG (getestet, nicht angenommen): eine direkte file://-URL wird von
// WebView2/WebKitGTK als <video src="..."> abgelehnt (Cross-Origin - die
// Wails-Oberfläche läuft unter einem eigenen internen Ursprung, nicht
// file://). darum läuft ein winziger lokaler HTTP-Server (127.0.0.1, fester
// Port pro Programmlauf), der die aktuell gewählte Videodatei mit
// Range-Request-Unterstützung ausliefert (nötig fürs Spulen im Video) -
// http.ServeFile() übernimmt Range-Handling automatisch korrekt.
//
// The query token (?v=<epoch>) changes whenever the active path changes.
// Without it, assigning the same http://127.0.0.1:port/video string to
// videoEl.src after SetPlaybackVideo leaves the browser on the first file.
func (a *App) VideoFileURL() string {
	a.stateMu.RLock()
	path := a.videoPath
	epoch := a.videoURLEpoch
	a.stateMu.RUnlock()
	if path == "" {
		return ""
	}
	port, err := a.ensureVideoServer()
	if err != nil {
		logging.Error("app: local video server could not be started", "error", err)
		return ""
	}
	return fmt.Sprintf("http://127.0.0.1:%d/video?v=%d", port, epoch)
}

// SetPlaybackVideo verknüpft ein beliebiges Video mit dem geladenen Skript
// (wenn keines mit gleichem Namen daneben liegt). Liefert die neue Video-URL.
func (a *App) SetPlaybackVideo(path string) (string, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return "", fmt.Errorf("no video path")
	}
	info, err := os.Stat(path)
	if err != nil || info.IsDir() {
		return "", fmt.Errorf("video file not found: %s", path)
	}
	a.stateMu.Lock()
	a.setVideoPathLocked(path)
	a.stateMu.Unlock()
	logging.Info("app: playback video set", "path", path)
	url := a.VideoFileURL()
	if url == "" {
		return "", fmt.Errorf("video server cannot be started")
	}
	return url, nil
}

// ClearPlaybackVideo entfernt die Video-Verknüpfung (Skript allein).
func (a *App) ClearPlaybackVideo() {
	a.stateMu.Lock()
	a.setVideoPathLocked("")
	a.stateMu.Unlock()
}

// ensureVideoServer startet (einmalig, beim ersten Bedarf) einen lokalen
// HTTP-Server, der ausschließlich an localhost lauscht und nur die aktuell
// in a.videoPath hinterlegte Datei ausliefert - kein offener Dateiserver,
// da der Pfad serverseitig bestimmt wird, nicht vom Client.
func (a *App) ensureVideoServer() (int, error) {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	if a.videoServerPort != 0 {
		return a.videoServerPort, nil
	}

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	port := ln.Addr().(*net.TCPAddr).Port

	mux := http.NewServeMux()
	mux.HandleFunc("/video", a.servePlaybackVideo)
	mux.HandleFunc("/training-video", a.serveTrainingVideo)
	srv := &http.Server{Handler: mux}
	go func() {
		if err := srv.Serve(ln); err != nil && err != http.ErrServerClosed {
			logging.Error("app: video server exited", "error", err)
		}
	}()

	a.videoServerPort = port
	logging.Info("app: local video server started", "port", port)
	return port, nil
}

// tryStartSession sorgt dafür, dass Wiedergabe und Training sich nicht
// gegenseitig überschreiben können - beide nutzen dasselbe physische Gerät,
// zwei gleichzeitig laufende Sitzungen ergeben keinen Sinn und würden sich
// sonst denselben playCancel teilen (die zuerst gestartete Sitzung wäre
// dann über die UI nicht mehr stoppbar). Gibt bei Erfolg einen neuen,
// abbrechbaren Context zurück.
func (a *App) tryStartSession() (context.Context, error) {
	a.stateMu.Lock()
	defer a.stateMu.Unlock()
	if a.sessionActive {
		return nil, fmt.Errorf("playback or training is already running — stop it first")
	}
	// Eine bestehende Testverbindung aus dem Geräte-Tab wird NICHT mehr
	// abgelehnt, sondern von claimSessionDevice() für die Sitzung
	// wiederverwendet (dasselbe *device.Device-Objekt, keine zweite BLE-
	// Verbindung) - siehe dessen Kommentar. Früher musste man hier erst im
	// Geräte-Tab trennen, nur um dieselbe Verbindung sofort danach für
	// Wiedergabe/Training neu aufzubauen.
	ctx, cancel := context.WithCancel(context.Background())
	a.sessionActive = true
	a.playCancel = cancel
	return ctx, nil
}

// claimSessionDevice liefert das Gerät, das eine neue Wiedergabe-/
// Trainingssitzung nutzen soll. Ist im Geräte-Tab bereits eine Testverbindung
// aufgebaut, wird GENAU dieses *device.Device-Objekt wiederverwendet
// (reused=true) - BLE erlaubt ohnehin nur eine Verbindung zum Gerät, eine
// zweite eigene Verbindung würde scheitern oder die erste stumm ersetzen.
// Der Aufrufer darf ein wiederverwendetes Gerät dann NICHT selbst verbinden
// oder trennen - das bleibt Sache des Geräte-Tabs (ConnectDevice/
// DisconnectDevice), das dieselbe Sperre gegen einen laufenden Session-Zugriff
// hat (siehe DisconnectDevice). Ohne bestehende Testverbindung wird wie
// bisher ein neues, eigenes Gerät erzeugt (reused=false), das der Aufrufer
// selbst verbindet und am Ende der Sitzung wieder trennt.
func (a *App) claimSessionDevice(mock bool) (dev device.Device, reused bool) {
	a.stateMu.RLock()
	testDev := a.testDevice
	a.stateMu.RUnlock()
	if testDev != nil {
		return testDev, true
	}
	if mock {
		return device.NewMock(false), false
	}
	return device.NewSamNeo2(device.SamNeo2Protocol{}), false
}

// endSession markiert die aktuelle Sitzung als beendet - muss von JEDEM
// Beendigungspfad aufgerufen werden (normales Ende, Fehler, Stop-Klick),
// sonst bleibt die App fälschlich im "sessionActive"-Zustand hängen und
// lehnt jede weitere Wiedergabe/jedes Training ab.
func (a *App) endSession() {
	a.stateMu.Lock()
	a.sessionActive = false
	a.playCancel = nil
	a.stateMu.Unlock()
}

// stopSession bricht die laufende Sitzung ab (falls eine läuft) - von
// StopPlayback/StopTraining aufgerufen. Beide dürfen dieselbe Methode
// nutzen, da ohnehin nur eine Sitzungsart gleichzeitig laufen kann.
func (a *App) stopSession() {
	a.stateMu.Lock()
	cancel := a.playCancel
	a.stateMu.Unlock()
	if cancel != nil {
		cancel()
	}
}

// shutdown läuft beim Schließen des Fensters. Drei Dinge müssen hier
// passieren: ein noch verbundenes Testgerät abschalten und trennen, eine
// noch laufende Wiedergabe-/Trainings-Sitzung ebenso beenden - sonst läuft
// das Gerät nach dem Schließen weiter, und die BLE-Verbindung bleibt offen,
// bis das Gerät manuell aus- und wieder eingeschaltet wird - und der Cache
// leeren, falls eingestellt.
//
// Der Sitzungs-Zweig fehlte vorher komplett: shutdown() kümmerte sich nur um
// a.testDevice (die Geräte-Tab-Testverbindung), nie um a.activeDevice/
// a.playCancel. Schloss man das Fenster während einer laufenden Wiedergabe
// oder eines Trainings, lief die Hintergrund-Goroutine (StartPlayback/
// StartTraining) einfach weiter, auch nachdem der Prozess beendet werden
// sollte - das Gerät blieb aktiv, die Verbindung offen.
//
// registerFileDrop meldet fallengelassene Dateien ans Frontend.
//
// Die Zuordnung passiert bewusst hier und nicht im Frontend: nur Go kennt
// den echten Dateipfad. Die Webview bekämme aus einem Drop lediglich einen
// Blob ohne Pfad, mit dem der Generator nichts anfangen kann.
func (a *App) registerFileDrop() {
	runtime.OnFileDrop(a.ctx, func(x, y int, paths []string) {
		if len(paths) == 0 {
			return
		}
		var videos, scripts []string
		for _, path := range paths {
			switch strings.ToLower(filepath.Ext(path)) {
			case ".funscript", ".samn":
				scripts = append(scripts, path)
			case ".mp4", ".mkv", ".avi", ".mov", ".m4v", ".webm", ".wmv", ".mpg", ".mpeg":
				videos = append(videos, path)
			}
		}
		logging.Info("app: files dropped", "videos", len(videos), "scripts", len(scripts))
		runtime.EventsEmit(a.ctx, "files:dropped", map[string]any{
			"videos":  videos,
			"scripts": scripts,
			"ignored": len(paths) - len(videos) - len(scripts),
		})
	})
}

func (a *App) shutdown(ctx context.Context) {
	a.stateMu.Lock()
	dev := a.testDevice
	a.testDevice = nil
	a.testDeviceMock = false
	a.testDeviceTransport = ""
	activeDev := a.activeDevice
	cancel := a.playCancel
	a.stateMu.Unlock()

	// Erst den Kontext abbrechen, damit die Sitzungs-Goroutine (falls noch
	// aktiv) selbst zu unwinden beginnt - ihre eigenen defer dev.Stop()/
	// dev.Disconnect() laufen dann ohnehin. Das direkte Stop()/Disconnect()
	// unten ist trotzdem nötig: shutdown() wartet nicht auf die Goroutine
	// (Wails erwartet hier keine Blockierung), das Gerät soll aber schon
	// jetzt sicher abgeschaltet sein, nicht erst "irgendwann danach". Beide
	// Aufrufe auf demselben Gerät sind unproblematisch - Disconnect() ist
	// idempotent (SamNeo2.Disconnect prüft, ob überhaupt noch verbunden).
	if cancel != nil {
		cancel()
	}
	// Ein laufendes KI-Training (kann Stunden dauern) nicht als
	// unsichtbaren Python-Prozess weiterlaufen lassen.
	a.CancelRoiTraining()
	a.CancelGoldenClipBenchmark()
	if dev != nil {
		_ = dev.Stop()
		_ = dev.Disconnect()
		logging.Info("app: test connection disconnected on shutdown")
	}
	if activeDev != nil {
		_ = activeDev.Stop()
		_ = activeDev.Disconnect()
		logging.Info("app: active session disconnected on shutdown")
	}
	a.clearCacheIfRequested()
}
