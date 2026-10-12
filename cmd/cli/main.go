// Command SamNPlayer spielt eine .funscript-Datei auf einem
// SVAKOM Sam Neo 2 / Sam Neo 2 Pro (oder, mit --mock, nur in der Konsole) ab.
//
// Protokoll (device/protocol.go, SamNeo2Protocol) ist gegen den offiziellen
// Buttplug-Rust-Quellcode verifiziert, siehe Kommentare dort für die genauen
// Quellenpfade.
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/player"
	"github.com/funfunpayer/SamNPlayer/sam"
)

func main() {
	// Subcommand: phase <a.funscript> <b.funscript> — FunGen/SamNPlayer
	// lag search without Python (funscript.BestLagCorrelation).
	if len(os.Args) >= 2 && os.Args[1] == "phase" {
		os.Exit(runPhase(os.Args[2:]))
	}
	if len(os.Args) >= 2 && (os.Args[1] == "benchmark" || os.Args[1] == "score") {
		os.Exit(runBenchmark(os.Args[2:]))
	}
	if len(os.Args) >= 2 && (os.Args[1] == "compare" || os.Args[1] == "fungen-compare") {
		os.Exit(runCompare(os.Args[2:]))
	}
	if len(os.Args) >= 2 && os.Args[1] == "generate" {
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		code := runGenerateContext(ctx, os.Args[2:])
		stop()
		os.Exit(code)
	}
	if len(os.Args) >= 2 && os.Args[1] == "sam" {
		os.Exit(runSam(os.Args[2:]))
	}
	if len(os.Args) >= 2 && (os.Args[1] == "stroke-preview" || os.Args[1] == "strokepreview") {
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		code := runStrokePreviewContext(ctx, os.Args[2:])
		stop()
		os.Exit(code)
	}
	if len(os.Args) >= 2 && os.Args[1] == "scan-scene-map" {
		os.Exit(runScanSceneMap(os.Args[2:]))
	}
	if len(os.Args) >= 2 && os.Args[1] == "import-contact-candidates" {
		os.Exit(runImportContactCandidates(os.Args[2:]))
	}
	if len(os.Args) >= 2 && (os.Args[1] == "export-learning" || os.Args[1] == "scene-map-export") {
		os.Exit(runExportLearning(os.Args[2:]))
	}

	scriptPath := flag.String("script", "", "Pfad zur .funscript-Datei (Pflicht)")
	mock := flag.Bool("mock", false, "Kein BLE - Befehle nur auf der Konsole ausgeben")
	verbose := flag.Bool("v", false, "Ausführliche Statusausgaben während der Wiedergabe")
	tickMs := flag.Int64("tick", 50, "Resampling-Intervall in ms (kleiner = feiner, mehr BLE-Writes)")
	maxSpeed := flag.Float64("max-speed", 0.6, "Positionsänderung (pos/ms), die als volle Intensität gilt")
	syncModeStr := flag.String("sync", "independent",
		"Wie Vibration/Sog zueinander stehen: independent, synchronized, alternating")

	extendedOEnabled := flag.Bool("extended-o", true,
		"Extended-O per [Enter] während der Wiedergabe aktivieren")
	extendedOMin := flag.Float64("extended-o-min", 0.1,
		"Amplitudenfaktor während Extended-O (0-1); Kurve/Rhythmus unverändert, nur niedriger")
	extendedOHold := flag.Duration("extended-o-hold", 10*time.Second, "Haltedauer für Extended-O (z.B. 10s, 1m)")
	extendedORestore := flag.Duration("extended-o-restore", 500*time.Millisecond,
		"Rampzeit auf/ab den Amplitudenfaktor bei Extended-O (0 = sofort)")

	muteContact := flag.Bool("mute-contact", false,
		"Kontakt-Vibration nur für diese Wiedergabe aus (Datei unverändert)")
	contactIntensity := flag.Float64("contact-intensity", 1,
		"Live-Skalierung der Kontakt-Vibration 0–2 (1=unverändert, ohne Datei-Rewrite)")
	contactExtraSmooth := flag.Float64("contact-extra-smooth", 0,
		"Zusätzliche EMA nur auf Vib/Sog nach dem Mapping (0=aus)")
	contactSpan := flag.Float64("contact-span", 0,
		"Live-Empfindlichkeit 0.4–0.95 (0=aus DeviceRecipe)")
	contactCurve := flag.String("contact-curve", "",
		"Live-Kurve linear|soft|peak (leer=aus DeviceRecipe)")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n  %s --script FILE [playback options]\n  %s phase A.funscript B.funscript [--max-lag-ms N] [--window-ms N]\n  %s benchmark --reference REF.funscript --candidate CAND.funscript [--video VIDEO] [--json] [--labels-out FILE]\n  %s compare --dataset DIR [--output report.md] [--max-lag-ms N]\n  %s generate --video FILE (--roi x,y,w,h | --scene-proposals FILE.scene.json) [--output FILE]\n  %s sam FILE.funscript [--output FILE.sam]\n  %s stroke-preview VIDEO [--json] [--max-seconds N]\n  %s export-learning FILE.samn --opt-in [--output-dir DIR]\n  %s import-contact-candidates FILE.samn --contact FILE.contact.json [--min-agree N]\n  %s scan-scene-map VIDEO [--windows N] [--out FILE]\n\n", os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0])
		fmt.Fprintf(os.Stderr, "Playback options:\n")
		flag.PrintDefaults()
	}

	flag.Parse()

	if *scriptPath == "" {
		fmt.Fprintln(os.Stderr, "Fehler: --script ist erforderlich (oder: phase A.funscript B.funscript)")
		flag.Usage()
		os.Exit(1)
	}

	syncMode, err := funscript.ParseSyncMode(*syncModeStr)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Fehler:", err)
		os.Exit(1)
	}

	script, err := funscript.Load(*scriptPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler beim Laden des Skripts: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Skript geladen: %d Actions, Dauer %.1fs\n",
		len(script.Actions), float64(script.Duration())/1000)

	opts := funscript.DefaultMapOptions()
	opts.TickMs = *tickMs
	opts.MaxSpeed = *maxSpeed
	opts.Sync = syncMode
	contactOn := false
	if funscript.IsDistanceProfile(script.Metadata.Profile) {
		opts = funscript.RecipeFor(script.Metadata.Profile)
		opts.TickMs = *tickMs
		if *maxSpeed > 0 {
			opts.MaxSpeed = *maxSpeed
		}
		if dr := script.Metadata.DeviceRecipe; dr != nil {
			opts.ContactVibration = dr.ContactVibration
			opts.ContactVibrationSpan = dr.ContactVibrationSpan
			opts.ContactVibrationCurve = dr.ContactVibrationCurve
		}
		if *contactSpan > 0 {
			opts.ContactVibrationSpan = *contactSpan
		}
		if *contactCurve != "" {
			opts.ContactVibrationCurve = *contactCurve
		}
		if len(script.Metadata.TrackingGaps) > 0 {
			opts.TrackingGaps = append([]funscript.TrackingGap(nil), script.Metadata.TrackingGaps...)
		}
		if *muteContact {
			opts.ContactVibration = false
		}
		contactOn = opts.ContactVibration
		if *syncModeStr != "" && *syncModeStr != "independent" {
			opts.Sync = syncMode
		}
	}
	var frames []funscript.Frame
	if contactOn {
		if s, err := sam.LoadSidecarIfPresent(*scriptPath); err == nil && s != nil && sam.HasContactIntensity(s) {
			frames = sam.ToDeviceFrames(sam.Densify(s, opts.TickMs, opts), opts)
		}
		if len(frames) == 0 {
			frames = sam.PlaybackFramesFromFunscript(script, opts)
		}
		fmt.Println("Kontakt-Vibration: SAM-Pfad (Densify + Intensity)")
	} else {
		frames = script.ToIntensityCurve(opts)
	}
	frames = sam.AdjustDeviceFrames(frames, sam.RuntimeAdjust{
		IntensityScale: *contactIntensity,
		ExtraSmooth:    *contactExtraSmooth,
		MuteContact:    *muteContact && contactOn,
	})
	fmt.Printf("In %d Steuer-Frames umgerechnet (alle %dms, sync=%s)\n", len(frames), *tickMs, opts.Sync)

	var dev device.Device
	if *mock {
		dev = device.NewMock(*verbose)
	} else {
		dev = device.NewSamNeo2(device.SamNeo2Protocol{})
	}

	// Ctrl+C sauber abfangen und Gerät stoppen statt hart abzubrechen.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		fmt.Println("\nAbbruch angefordert, stoppe...")
		cancel()
	}()

	connectCtx, connectCancel := context.WithTimeout(ctx, 20*time.Second)
	defer connectCancel()
	if err := dev.Connect(connectCtx); err != nil {
		fmt.Fprintf(os.Stderr, "Verbindung fehlgeschlagen: %v\n", err)
		os.Exit(1)
	}
	defer dev.Disconnect()

	p := player.New(dev)
	if *verbose {
		p.LogEvery = time.Second
	}

	if *extendedOEnabled {
		fmt.Printf("Extended-O: [Enter] drücken für %.0f%% Amplitude (Rhythmus gleich), %s halten\n",
			*extendedOMin*100, *extendedOHold)
		go func() {
			scanner := bufio.NewScanner(os.Stdin)
			for scanner.Scan() {
				p.TriggerExtendedO(player.ExtendedOOptions{
					MinLevel:        *extendedOMin,
					HoldDuration:    *extendedOHold,
					RestoreDuration: *extendedORestore,
				})
			}
		}()
	}

	fmt.Println("Wiedergabe startet...")
	if err := p.Play(ctx, frames); err != nil && err != context.Canceled {
		fmt.Fprintf(os.Stderr, "Fehler während der Wiedergabe: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("Fertig.")
}

// runPhase compares two funscripts (reference vs candidate) via
// BestLagCorrelation + DiagnosePhase. With --window-ms > 0, runs
// WindowedBestLagCorrelation and prints per-window lag/orientation/r
// (docs/FINDINGS_TIMING_TF.md next slice).
// Args: A.funscript B.funscript [--max-lag-ms N] [--window-ms N] ...
func runPhase(args []string) int {
	fs := flag.NewFlagSet("phase", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	maxLag := fs.Int("max-lag-ms", funscript.DefaultMaxLagMs, "Lag-Suchfenster ±ms")
	lagStep := fs.Int("lag-step-ms", funscript.DefaultLagStepMs, "Lag-Schrittweite ms")
	resample := fs.Int("resample-ms", funscript.DefaultResampleStepMs, "Resample-Schrittweite ms")
	windowMs := fs.Int("window-ms", 0, "Window size ms for per-window lag search (0 = whole-clip only)")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s phase A.funscript B.funscript [options]\n", os.Args[0])
		fs.PrintDefaults()
	}

	var paths, flagArgs []string
	paths, flagArgs = splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if len(paths) != 2 {
		fs.Usage()
		return 2
	}
	a, err := funscript.Load(paths[0])
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler A: %v\n", err)
		return 1
	}
	b, err := funscript.Load(paths[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler B: %v\n", err)
		return 1
	}

	if *windowMs > 0 {
		return runPhaseWindowed(a.Actions, b.Actions, *windowMs, *maxLag, *lagStep, *resample)
	}

	mf := funscript.EvaluateMotionFidelity(a.Actions, b.Actions, *maxLag, *lagStep, *resample)
	diag := mf.Diagnosis
	if mf.R == nil {
		fmt.Printf("kind=motion_fidelity\nverdict=%s\n%s\n", diag.Verdict, diag.Detail)
		return 0
	}
	r0 := "n/a"
	if mf.RZeroLag != nil {
		r0 = fmt.Sprintf("%.4f", *mf.RZeroLag)
	}
	fmt.Printf("kind=motion_fidelity\n")
	fmt.Printf("verdict=%s\n", diag.Verdict)
	fmt.Printf("detail=%s\n", diag.Detail)
	fmt.Printf("r=%.4f r_zero_lag=%s lag_ms=%d orientation=%s low_confidence=%v aliasing_risk=%v",
		*mf.R, r0, *mf.LagMs, mf.Orientation, mf.LowConfidence, mf.AliasingRisk)
	if mf.AliasingRisk {
		fmt.Printf(" dominant_period_ms=%d alternate_lags_ms=%v", mf.DominantPeriodMs, mf.AlternateLagsMs)
	}
	fmt.Printf("\n")
	return 0
}

func runPhaseWindowed(a, b []funscript.Action, windowMs, maxLag, lagStep, resample int) int {
	windows := funscript.WindowedBestLagCorrelation(a, b, windowMs, maxLag, lagStep, resample)
	fmt.Printf("kind=motion_fidelity_windowed\nwindow_ms=%d\n", windowMs)
	var (
		confident int
		sumR      float64
		minLag    int
		maxLagW   int
		haveLag   bool
		orients   = map[string]int{}
	)
	for _, w := range windows {
		ws := float64(w.WindowStartMs) / 1000
		we := float64(w.WindowEndMs) / 1000
		if w.Correlation == nil {
			fmt.Printf("window %.1f-%.1fs: undefined (%s)\n", ws, we, w.Reason)
			continue
		}
		c := w.Correlation
		low := ""
		if c.LowConfidence {
			low = " [LOW CONFIDENCE]"
		}
		inv := ""
		if c.Orientation == "inverted" {
			inv = " INVERTED"
		}
		alias := ""
		if c.AliasingRisk {
			alias = fmt.Sprintf(" [ALIASING RISK period≈%dms alts=%v]", c.DominantPeriodMs, c.AlternateLagsMs)
		}
		fmt.Printf("window %.1f-%.1fs: r=%.3f @ lag %+dms%s%s%s\n", ws, we, c.R, c.LagMs, inv, low, alias)
		if !c.LowConfidence {
			confident++
			sumR += c.R
			orients[c.Orientation]++
			if !haveLag {
				minLag, maxLagW, haveLag = c.LagMs, c.LagMs, true
			} else {
				if c.LagMs < minLag {
					minLag = c.LagMs
				}
				if c.LagMs > maxLagW {
					maxLagW = c.LagMs
				}
			}
		}
	}
	if confident > 0 {
		fmt.Printf("summary: n=%d confident of %d total, mean_r=%.3f, lag_range=%+d..%+dms\n",
			confident, len(windows), sumR/float64(confident), minLag, maxLagW)
		if len(orients) > 1 {
			fmt.Printf("summary: orientation FLIPS across the clip (normal=%d inverted=%d)\n",
				orients["normal"], orients["inverted"])
		}
	} else {
		fmt.Printf("summary: no confident windows\n")
	}
	return 0
}

// runCompare is the thin FunGen-compare CLI (fungen_compare.py compare half)
// on top of funscript.CompareDataset / BestLagCorrelation.
func runCompare(args []string) int {
	fs := flag.NewFlagSet("compare", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	dataset := fs.String("dataset", "", "Folder with FunGen .funscript refs and *__hub/tf/tj.funscript batch output")
	output := fs.String("output", "", "Write markdown report here (default: stdout)")
	maxLag := fs.Int("max-lag-ms", funscript.DefaultMaxLagMs, "Lag search window ±ms")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s compare --dataset DIR [--output report.md] [--max-lag-ms N]\n", os.Args[0])
		fs.PrintDefaults()
	}
	paths, flagArgs := splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if len(paths) > 0 {
		fmt.Fprintln(os.Stderr, "Unerwartete Positionsargumente:", paths)
		fs.Usage()
		return 2
	}
	if *dataset == "" {
		fs.Usage()
		return 2
	}
	result, err := funscript.CompareDataset(*dataset, *maxLag)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	report := funscript.FormatCompareReport(result)
	if *output != "" {
		if err := os.WriteFile(*output, []byte(report), 0644); err != nil {
			fmt.Fprintf(os.Stderr, "Schreiben fehlgeschlagen: %v\n", err)
			return 1
		}
		fmt.Fprintf(os.Stderr, "Geschrieben: %s\n", *output)
		return 0
	}
	fmt.Print(report)
	return 0
}
