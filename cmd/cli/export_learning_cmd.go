package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/funfunpayer/SamNPlayer/generator"
)

// runExportLearning: SceneMap P5a — local L0 collect from .samn sceneMap.
// Requires --opt-in. Never writes YOLO images/labels/train.
func runExportLearning(args []string) int {
	fs := flag.NewFlagSet("export-learning", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	optIn := fs.Bool("opt-in", false, "Required: confirm Collect learning data (default off)")
	outDir := fs.String("output-dir", "", "Root for scene_map_learning (default: …/roi_training_dataset/scene_map_learning)")
	del := fs.Bool("delete", false, "Delete only scene_map_learning under --output-dir / default dataset root")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n  %s export-learning FILE.samn --opt-in [--output-dir DIR]\n  %s export-learning --delete [--output-dir DATASET_ROOT]\n\n", os.Args[0], os.Args[0])
		fmt.Fprintf(os.Stderr, "Exports engine_trace.jsonl, negatives.json, auto_candidates.jsonl\n")
		fmt.Fprintf(os.Stderr, "(reviewed:false), user_region_marks.json from metadata.sceneMap.\n")
		fmt.Fprintf(os.Stderr, "Does not write images/train or labels/train.\n")
		fs.PrintDefaults()
	}
	paths, flagArgs := splitCLIArgs(fs, args)
	if err := fs.Parse(flagArgs); err != nil {
		return 2
	}
	if *del {
		if len(paths) != 0 {
			fs.Usage()
			return 2
		}
		root := *outDir
		if root == "" {
			root = generator.DefaultRoiDatasetDir()
		}
		if err := generator.DeleteSceneMapLearningData(root); err != nil {
			fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
			return 1
		}
		fmt.Println("Deleted scene_map_learning under", root)
		return 0
	}
	if len(paths) != 1 {
		fs.Usage()
		return 2
	}
	res, err := generator.ExportSceneMapLearning(paths[0], generator.LearningExportOptions{
		OptIn:     *optIn,
		OutputDir: *outDir,
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fehler: %v\n", err)
		return 1
	}
	fmt.Printf("Exported %d windows → %s\n", res.Windows, res.OutDir)
	fmt.Printf("  negatives=%d auto_candidates=%d (reviewed:false) user_regions=%d\n",
		res.Negatives, res.AutoCandidates, res.UserRegionMarks)
	return 0
}
