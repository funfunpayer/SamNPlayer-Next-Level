package main

import (
	"log/slog"
	"os"
	"path/filepath"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/funfunpayer/SamNPlayer/generator"
	"github.com/funfunpayer/SamNPlayer/generator/bodyparts"
	"github.com/funfunpayer/SamNPlayer/logging"
)

const (
	prefUpdateCheckOnStartup = "update.check_on_startup"
	prefLogLevel             = "log.level"
	prefReportPath           = "generator.reportPath"
	prefClearCacheOnExit     = "cache.clearOnExit"
	prefDeviceTransport      = "device.transport"
	prefIntifaceURL          = "device.intifaceUrl"
	prefTCodeAddress         = "device.tcodeAddress"
	prefDeviceConnectTest    = "device.connect_test"
	prefAIRoiModelPath       = "generator.aiRoiModelPath"
	prefAIPreferredClasses   = "generator.aiPreferredClasses"
	prefAIBaseURL            = "generator.aiBaseUrl"
	prefBenchmarkManifest    = "generator.benchmarkManifestPath"
	prefBenchmarkHistoryPath = "generator.benchmarkHistoryPath"
	prefDiagnosticsHistory   = "device.diagnosticsHistoryPath"
	prefRoiDatasetDir        = "generator.roiTrainingDatasetDir"
	// prefCollectLearningData: SceneMap P5 — local L0 collect (default OFF).
	prefCollectLearningData = "generator.collectLearningData"
	// prefApplyAISetupAutomatically: Scene2 — fill empty Tip/ROI2 from
	// companion .scene.json when soft-loaded (default OFF; Owner 28 Sep).
	prefApplyAISetupAutomatically = "generator.applyAISetupAutomatically"

	// Create Feel + Expert knobs (MakeVib-style). Defaults match HTML.
	// Does not flip Everyday on/off or CSRT path — remembers user tweaks only.
	prefGenContactVibrationSpan  = "generator.contact_vibration_span"
	prefGenContactVibrationCurve = "generator.contact_vibration_curve"
	prefGenSmoothWindow          = "generator.smooth_window"
	prefGenMinPeakDistanceMs     = "generator.min_peak_distance_ms"
	prefGenPeakProminence        = "generator.peak_prominence"
	prefGenRDPTolerance          = "generator.rdp_tolerance"
	prefGenMaxSpeed              = "generator.max_speed"
	prefGenAdaptiveKeyframe      = "generator.adaptive_keyframe"
	// AI Train Sampling + Box scale (defaults match HTML / #400).
	prefRoiSampleEvery = "generator.roi_sample_every"
	prefRoiBoxScale    = "generator.roi_box_scale"

	prefPlaybackMock        = "playback.mock"
	prefPlaybackSync        = "playback.sync_mode"
	prefPlaybackTickMs      = "playback.tick_ms"
	prefPlaybackMaxSpeed    = "playback.max_speed"
	prefPlaybackSmoothing   = "playback.smoothing"
	prefPlaybackSoftStartMs = "playback.soft_start_ms"
	prefPlaybackEOEnabled   = "playback.extended_o_enabled"
	prefPlaybackEOMin       = "playback.extended_o_min"
	prefPlaybackEOHoldS     = "playback.extended_o_hold_seconds"
	prefPlaybackEORestoreMs = "playback.extended_o_restore_ms"
	// prefPlaybackEffectPresets: Player → Inspector → Effects presets, a JSON
	// object {name: {intensity, smoothing, maxSpeed, tickMs, softStartMs,
	// reducePeaks}} written by the GUI. Stored as text so the GUI owns the shape.
	prefPlaybackEffectPresets = "playback.effect_presets"
	// prefPlaybackTrajectoryOverlay: MT-Debug — zeigt die aufgezeichnete
	// Tip/Partner-Trajektorie (falls im Skript vorhanden) als Polylinie
	// über dem Video. Default aus (Debug-Werkzeug, nicht jedes Skript hat
	// die Daten).
	prefPlaybackTrajectoryOverlay = "playback.trajectory_overlay"
	// prefPlaybackVideoPlayAutostart: startet das native <video>-Play-Event
	// (Browser-eigener Button/Leertaste mit Fokus auf dem Video) auch die
	// Geräte-/Kurven-Wiedergabe. Getrennt von "Device follows video
	// position" (nur Positions-Sync während der Wiedergabe) - vorher lief
	// das ungefragt immer mit, auch bei ausgeschaltetem Sync.
	prefPlaybackVideoPlayAutostart = "playback.video_play_autostart"
	// prefPlaybackHonorInverted: OFS/funscript top-level "inverted": true
	// flips positions (100−pos) in Play (#453). Switchable (Owner 30 Sep):
	// some files carry the flag although their authors never meant players
	// to flip them. Default on = behavior since v0.5.44.
	prefPlaybackHonorInverted = "playback.honor_inverted"

	// Play OFS / Feel display+edit knobs (MakeVib-style). Defaults match HTML.
	// Not Create / Everyday CSRT. BPM number stays blank=auto (not persisted).
	prefPlaybackCapIntensity         = "playback.cap_intensity"
	prefPlaybackSpeedHL              = "playback.speed_highlights"
	prefPlaybackSpeedHLThresh        = "playback.speed_highlight_thresh"
	prefPlaybackScaleFactor          = "playback.scale_factor"
	prefPlaybackScaleSoftEdges       = "playback.scale_soft_edges"
	prefPlaybackFpsSnap              = "playback.fps_snap"
	prefPlaybackBpmGrid              = "playback.bpm_grid"
	prefPlaybackOMarkerIntensity     = "playback.omarker_intensity"
	prefPlaybackFeelHeatbands        = "playback.feel_heatbands"
	prefPlaybackFeelHeatbandsOpacity = "playback.feel_heatbands_opacity"
	prefPlaybackContactIntensity     = "playback.contact_intensity"

	prefTrainingMock                = "training.mock"
	prefTrainingTechnique           = "training.technique"
	prefTrainingChannel             = "training.channel"
	prefTrainingCycles              = "training.cycles"
	prefTrainingRampUpMs            = "training.ramp_up_ms"
	prefTrainingHoldMs              = "training.hold_ms"
	prefTrainingRestMs              = "training.rest_ms"
	prefTrainingPeakIntensity       = "training.peak_intensity"
	prefTrainingPlateauFraction     = "training.plateau_fraction"
	prefTrainingProgressionPerCycle = "training.progression_per_cycle"
)

// Settings wird komplett auf einmal ans Frontend gegeben und zurückgenommen -
// einfacher als viele einzelne Get/Set-Aufrufe über die Wails-Bindung.
type Settings struct {
	UpdateCheckOnStartup bool    `json:"updateCheckOnStartup"`
	LogLevel             string  `json:"logLevel"`
	PlaybackMock         bool    `json:"playbackMock"`
	PlaybackSync         string  `json:"playbackSync"`
	PlaybackTickMs       float64 `json:"playbackTickMs"`
	PlaybackMaxSpeed     float64 `json:"playbackMaxSpeed"`
	PlaybackSmoothing    float64 `json:"playbackSmoothing"`
	PlaybackSoftStartMs  float64 `json:"playbackSoftStartMs"`
	PlaybackEOEnabled    bool    `json:"playbackEOEnabled"`
	PlaybackEOMin        float64 `json:"playbackEOMin"`
	PlaybackEOHoldS      float64 `json:"playbackEOHoldS"`
	PlaybackEORestoreMs  float64 `json:"playbackEORestoreMs"`
	// PlaybackEffectPresets: see prefPlaybackEffectPresets ("" = none saved).
	PlaybackEffectPresets string `json:"playbackEffectPresets"`
	// PlaybackVideoPlayAutostart: siehe prefPlaybackVideoPlayAutostart.
	// Default true - bisheriges Verhalten bleibt ohne Zutun erhalten.
	PlaybackVideoPlayAutostart bool `json:"playbackVideoPlayAutostart"`
	// PlaybackHonorInverted: siehe prefPlaybackHonorInverted. Default true.
	PlaybackHonorInverted bool `json:"playbackHonorInverted"`
	// PlaybackAudioVibration: siehe prefPlaybackAudioVibration. Default false.
	PlaybackAudioVibration bool `json:"playbackAudioVibration"`
	// PlaybackTrajectoryOverlay: siehe prefPlaybackTrajectoryOverlay.
	// Default false - Debug-Overlay, nicht jeder Nutzer/jedes Skript hat es.
	PlaybackTrajectoryOverlay bool `json:"playbackTrajectoryOverlay"`

	PlaybackCapIntensity         float64 `json:"playbackCapIntensity"`
	PlaybackSpeedHL              bool    `json:"playbackSpeedHL"`
	PlaybackSpeedHLThresh        float64 `json:"playbackSpeedHLThresh"`
	PlaybackScaleFactor          float64 `json:"playbackScaleFactor"`
	PlaybackScaleSoftEdges       bool    `json:"playbackScaleSoftEdges"`
	PlaybackFpsSnap              float64 `json:"playbackFpsSnap"`
	PlaybackBpmGrid              bool    `json:"playbackBpmGrid"`
	PlaybackOMarkerIntensity     float64 `json:"playbackOMarkerIntensity"`
	PlaybackFeelHeatbands        bool    `json:"playbackFeelHeatbands"`
	PlaybackFeelHeatbandsOpacity float64 `json:"playbackFeelHeatbandsOpacity"`
	PlaybackContactIntensity     float64 `json:"playbackContactIntensity"`

	TrainingMock                bool    `json:"trainingMock"`
	TrainingTechnique           string  `json:"trainingTechnique"`
	TrainingChannel             string  `json:"trainingChannel"`
	TrainingCycles              float64 `json:"trainingCycles"`
	TrainingRampUpMs            float64 `json:"trainingRampUpMs"`
	TrainingHoldMs              float64 `json:"trainingHoldMs"`
	TrainingRestMs              float64 `json:"trainingRestMs"`
	TrainingPeakIntensity       float64 `json:"trainingPeakIntensity"`
	TrainingPlateauFraction     float64 `json:"trainingPlateauFraction"`
	TrainingProgressionPerCycle float64 `json:"trainingProgressionPerCycle"`

	LogPath string `json:"logPath"`

	// ReportPath: Zieldatei für die Messwerte jedes Generatorlaufs.
	// Leer = kein Bericht. Frei wählbar, damit der Bericht dort landet, wo
	// er gebraucht wird - etwa neben der Videosammlung.
	ReportPath        string `json:"reportPath"`
	DefaultReportPath string `json:"defaultReportPath"`
	ClearCacheOnExit  bool   `json:"clearCacheOnExit"`

	// Verbindungsart und Serveradresse merken: wer Intiface auf dem Handy
	// laufen lässt, tippt sonst bei jedem Start dieselbe IP neu ein.
	DeviceTransport string `json:"deviceTransport"`
	IntifaceURL     string `json:"intifaceUrl"`
	TCodeAddress    string `json:"tcodeAddress"`
	// DeviceConnectTest: nach erfolgreichem Verbinden einmal kurz Vib/Sog
	// testen. Standard aus - stört sonst bei jedem Connect.
	DeviceConnectTest bool `json:"deviceConnectTest"`

	// AIRoiModelPath: Pfad zur .onnx-Modelldatei für die KI-Regionssuche
	// (generator/ai_roi.py). Leer = Standardordner (siehe
	// ai_roi.default_model_path()) - die meisten Nutzer legen die Datei
	// einfach dort ab, statt hier einen Pfad einzutragen. Ob die KI-Suche
	// nutzbar ist, prüft CheckAIRoiAvailable() bei Bedarf separat (kostet
	// einen Python-Start, gehört darum nicht in dieses Massen-Get).
	AIRoiModelPath string `json:"aiRoiModelPath"`

	// AIPreferredClasses: comma-separated class names or ids for AI ROI
	// (e.g. "hand,breast"). Empty = highest confidence among all classes.
	// Names resolve via classes.json next to the .onnx (copied on train).
	AIPreferredClasses string `json:"aiPreferredClasses"`

	// AIBaseURL: Adresse eines lokalen Colibri-Servers (coli serve) für
	// Profil-Vorschlag und KI-Zweitmeinung zur Qualität (docs/AI_ADAPTER.md).
	// Leer = colibri_client.DEFAULT_BASE_URL (Standard-Localhost-Port).
	AIBaseURL string `json:"aiBaseUrl"`

	// BenchmarkManifestPath: Pfad zur Manifest-Datei des Golden-Clip-
	// Benchmarks (siehe generator/golden_clip_benchmark.py) - zeigt auf
	// lokale Videos/Referenzen des Nutzers, liegt darum nicht im Repository
	// und hat keinen sinnvollen Standardwert.
	BenchmarkManifestPath string `json:"benchmarkManifestPath"`

	// BenchmarkHistoryPath: JSONL-Datei, an die jeder Benchmark-Lauf
	// angehängt wird - Standardort analog zu DefaultReportPath.
	BenchmarkHistoryPath        string `json:"benchmarkHistoryPath"`
	DefaultBenchmarkHistoryPath string `json:"defaultBenchmarkHistoryPath"`

	// DiagnosticsHistoryPath: JSONL-Datei, an die jeder Geräte-Diagnoselauf
	// angehängt wird (siehe app_diagnostics.go) - Standardort analog zu
	// DefaultBenchmarkHistoryPath.
	DiagnosticsHistoryPath        string `json:"diagnosticsHistoryPath"`
	DefaultDiagnosticsHistoryPath string `json:"defaultDiagnosticsHistoryPath"`

	// RoiDatasetDir: Ordner, in dem der Bootstrap-Trainingsdatensatz für die
	// KI-Regionserkennung wächst (siehe app_roi_training.go) - Standardort
	// analog zu DefaultBenchmarkHistoryPath.
	RoiDatasetDir        string `json:"roiDatasetDir"`
	DefaultRoiDatasetDir string `json:"defaultRoiDatasetDir"`

	// CollectLearningData: SceneMap P5 — write scene_map_learning JSON locally.
	// Default false (Owner: opt-in only). Never uploads; never YOLO train write.
	CollectLearningData bool `json:"collectLearningData"`

	// ApplyAISetupAutomatically: when a companion .scene.json soft-loads in
	// Create, fill empty Tip (and contact partner) from the proposal. Default
	// off — user Apply buttons remain the Everyday path. Undo in Create.
	ApplyAISetupAutomatically bool `json:"applyAISetupAutomatically"`

	// Create Feel + Expert knob prefs (defaults match HTML / Everyday).
	GenContactVibrationSpan  float64 `json:"genContactVibrationSpan"`
	GenContactVibrationCurve string  `json:"genContactVibrationCurve"`
	GenSmoothWindow          float64 `json:"genSmoothWindow"`
	GenMinPeakDistanceMs     float64 `json:"genMinPeakDistanceMs"`
	GenPeakProminence        float64 `json:"genPeakProminence"`
	GenRDPTolerance          float64 `json:"genRDPTolerance"`
	GenMaxSpeed              float64 `json:"genMaxSpeed"`
	GenAdaptiveKeyframe      bool    `json:"genAdaptiveKeyframe"`

	RoiSampleEvery float64 `json:"roiSampleEvery"`
	RoiBoxScale    float64 `json:"roiBoxScale"`
}

func (a *App) GetSettings() Settings {
	s := a.settings
	return Settings{
		UpdateCheckOnStartup:  s.GetBool(prefUpdateCheckOnStartup, true),
		LogLevel:              s.GetString(prefLogLevel, "info"),
		PlaybackMock:          s.GetBool(prefPlaybackMock, true),
		PlaybackSync:          s.GetString(prefPlaybackSync, "independent"),
		PlaybackTickMs:        s.GetFloat(prefPlaybackTickMs, 50),
		PlaybackMaxSpeed:      s.GetFloat(prefPlaybackMaxSpeed, 0.6),
		PlaybackSmoothing:     s.GetFloat(prefPlaybackSmoothing, 0.3),
		PlaybackSoftStartMs:   s.GetFloat(prefPlaybackSoftStartMs, 500),
		PlaybackEOEnabled:     s.GetBool(prefPlaybackEOEnabled, true),
		PlaybackEOMin:         s.GetFloat(prefPlaybackEOMin, 0.1),
		PlaybackEOHoldS:       s.GetFloat(prefPlaybackEOHoldS, 10),
		PlaybackEORestoreMs:   s.GetFloat(prefPlaybackEORestoreMs, 500),
		PlaybackEffectPresets: s.GetString(prefPlaybackEffectPresets, ""),

		PlaybackVideoPlayAutostart: s.GetBool(prefPlaybackVideoPlayAutostart, true),
		PlaybackHonorInverted:      s.GetBool(prefPlaybackHonorInverted, true),
		PlaybackAudioVibration:     s.GetBool(prefPlaybackAudioVibration, false),
		PlaybackTrajectoryOverlay:  s.GetBool(prefPlaybackTrajectoryOverlay, false),

		PlaybackCapIntensity:         s.GetFloat(prefPlaybackCapIntensity, 400),
		PlaybackSpeedHL:              s.GetBool(prefPlaybackSpeedHL, true),
		PlaybackSpeedHLThresh:        s.GetFloat(prefPlaybackSpeedHLThresh, 400),
		PlaybackScaleFactor:          s.GetFloat(prefPlaybackScaleFactor, 0.8),
		PlaybackScaleSoftEdges:       s.GetBool(prefPlaybackScaleSoftEdges, false),
		PlaybackFpsSnap:              s.GetFloat(prefPlaybackFpsSnap, 0),
		PlaybackBpmGrid:              s.GetBool(prefPlaybackBpmGrid, false),
		PlaybackOMarkerIntensity:     s.GetFloat(prefPlaybackOMarkerIntensity, 0.5),
		PlaybackFeelHeatbands:        s.GetBool(prefPlaybackFeelHeatbands, true),
		PlaybackFeelHeatbandsOpacity: s.GetFloat(prefPlaybackFeelHeatbandsOpacity, 1),
		PlaybackContactIntensity:     s.GetFloat(prefPlaybackContactIntensity, 1),

		TrainingMock:                s.GetBool(prefTrainingMock, true),
		TrainingTechnique:           s.GetString(prefTrainingTechnique, "stopstart"),
		TrainingChannel:             s.GetString(prefTrainingChannel, "vibration"),
		TrainingCycles:              s.GetFloat(prefTrainingCycles, 5),
		TrainingRampUpMs:            s.GetFloat(prefTrainingRampUpMs, 8000),
		TrainingHoldMs:              s.GetFloat(prefTrainingHoldMs, 3000),
		TrainingRestMs:              s.GetFloat(prefTrainingRestMs, 10000),
		TrainingPeakIntensity:       s.GetFloat(prefTrainingPeakIntensity, 0.8),
		TrainingPlateauFraction:     s.GetFloat(prefTrainingPlateauFraction, 0.7),
		TrainingProgressionPerCycle: s.GetFloat(prefTrainingProgressionPerCycle, 0.15),

		LogPath:            logging.Path(),
		ReportPath:         s.GetString(prefReportPath, ""),
		DefaultReportPath:  defaultReportPath(),
		ClearCacheOnExit:   s.GetBool(prefClearCacheOnExit, false),
		DeviceTransport:    s.GetString(prefDeviceTransport, "ble"),
		IntifaceURL:        s.GetString(prefIntifaceURL, ""),
		TCodeAddress:       s.GetString(prefTCodeAddress, ""),
		DeviceConnectTest:  s.GetBool(prefDeviceConnectTest, false),
		AIRoiModelPath:     s.GetString(prefAIRoiModelPath, ""),
		AIPreferredClasses: s.GetString(prefAIPreferredClasses, bodyparts.PreferredClassesCSV()),
		AIBaseURL:          s.GetString(prefAIBaseURL, ""),

		BenchmarkManifestPath:       s.GetString(prefBenchmarkManifest, ""),
		BenchmarkHistoryPath:        s.GetString(prefBenchmarkHistoryPath, ""),
		DefaultBenchmarkHistoryPath: defaultBenchmarkHistoryPath(),

		DiagnosticsHistoryPath:        s.GetString(prefDiagnosticsHistory, ""),
		DefaultDiagnosticsHistoryPath: defaultDiagnosticsHistoryPath(),

		RoiDatasetDir:        s.GetString(prefRoiDatasetDir, ""),
		DefaultRoiDatasetDir: generator.DefaultRoiDatasetDir(),

		CollectLearningData: s.GetBool(prefCollectLearningData, false),

		ApplyAISetupAutomatically: s.GetBool(prefApplyAISetupAutomatically, false),

		GenContactVibrationSpan:  s.GetFloat(prefGenContactVibrationSpan, 75),
		GenContactVibrationCurve: s.GetString(prefGenContactVibrationCurve, "soft"),
		GenSmoothWindow:          s.GetFloat(prefGenSmoothWindow, 11),
		GenMinPeakDistanceMs:     s.GetFloat(prefGenMinPeakDistanceMs, 150),
		GenPeakProminence:        s.GetFloat(prefGenPeakProminence, 0),
		GenRDPTolerance:          s.GetFloat(prefGenRDPTolerance, 0),
		GenMaxSpeed:              s.GetFloat(prefGenMaxSpeed, 0),
		GenAdaptiveKeyframe:      s.GetBool(prefGenAdaptiveKeyframe, true),

		RoiSampleEvery: s.GetFloat(prefRoiSampleEvery, 12),
		RoiBoxScale:    s.GetFloat(prefRoiBoxScale, 1.0),
	}
}

// SetSetting speichert einen einzelnen Wert - wird bei jeder Änderung eines
// Formularfelds im Frontend aufgerufen (statt alles auf einmal zu speichern,
// damit nichts verloren geht, falls die App zwischendurch beendet wird).
func (a *App) SetSetting(key string, value any) error {
	if key == prefLogLevel {
		if s, ok := value.(string); ok {
			logging.SetLevel(parseLogLevel(s))
		}
	}
	if err := a.settings.Set(key, value); err != nil {
		return err
	}
	// Das geladene Skript trägt den Schalter schon in sich (siehe
	// loadScriptDocument) - neu laden, damit Kurve und Gerät sofort folgen.
	if key == prefPlaybackHonorInverted && a.loadedScriptPath() != "" {
		return a.reloadLoadedScript()
	}
	// Audio-Vibration eingeschaltet: Ton schon jetzt im Hintergrund lesen,
	// damit der nächste Play-Start nicht auf ffmpeg wartet.
	if key == prefPlaybackAudioVibration && a.audioVibrationEnabled() {
		a.warmAudioVibration(int64(a.settings.GetFloat(prefPlaybackTickMs, 50)))
	}
	return nil
}

func (a *App) OpenLogFolder() error {
	dir, err := logging.Dir()
	if err != nil {
		return err
	}
	if _, err := os.Stat(dir); err != nil {
		return err
	}
	runtime.BrowserOpenURL(a.ctx, fileURL(dir))
	return nil
}

func parseLogLevel(s string) slog.Level {
	switch s {
	case "debug":
		return slog.LevelDebug
	case "warn":
		return slog.LevelWarn
	case "error":
		return slog.LevelError
	default:
		return slog.LevelInfo
	}
}

// defaultReportPath schlägt einen Ort neben der Logdatei vor.
func defaultReportPath() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "SamNPlayer", "messwerte.jsonl")
}

// defaultBenchmarkHistoryPath schlägt einen Ort neben den übrigen
// Nutzerdaten vor - analog zu defaultReportPath.
func defaultBenchmarkHistoryPath() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "SamNPlayer", "golden_clip_history.jsonl")
}

// defaultAIRoiModelPath bildet ai_roi.py's default_model_path() 1:1 in Go
// nach - GLEICHE Umgebungsvariablen, gleiche Fallback-Reihenfolge (NICHT
// os.UserConfigDir(), das unter Windows %AppData% statt %LOCALAPPDATA%
// liefert). CheckAIRoiAvailable() prüft am Ende genau den Pfad, den die
// Python-Funktion berechnet - ein frisch trainiertes Modell muss exakt
// dort landen, sonst findet die App ihr eigenes Trainingsergebnis nicht.
func defaultAIRoiModelPath() string {
	if local := os.Getenv("LOCALAPPDATA"); local != "" {
		return filepath.Join(local, "SamNPlayer", "models", "roi_detector.onnx")
	}
	xdg := os.Getenv("XDG_CONFIG_HOME")
	if xdg == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		xdg = filepath.Join(home, ".config")
	}
	return filepath.Join(xdg, "SamNPlayer", "models", "roi_detector.onnx")
}

// defaultDiagnosticsHistoryPath schlägt einen Ort neben den übrigen
// Nutzerdaten vor - analog zu defaultBenchmarkHistoryPath.
func defaultDiagnosticsHistoryPath() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "SamNPlayer", "device_diagnostics_history.jsonl")
}
