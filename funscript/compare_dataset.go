package funscript

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

var batchSuffixRE = regexp.MustCompile(`__(hub|tf|tj)\.funscript$`)

// CompareRow is one reference × kind comparison (fungen_compare.compare_dataset).
type CompareRow struct {
	Reference        string   `json:"reference"`
	Stem             string   `json:"stem"`
	Kind             string   `json:"kind"`
	R                float64  `json:"r"`
	LagMs            int      `json:"lag_ms"`
	Orientation      string   `json:"orientation"`
	NSamples         int      `json:"n_samples"`
	ShapeError       *float64 `json:"shape_error"`
	RZeroLag         *float64 `json:"r_zero_lag"`
	LowConfidence    bool     `json:"low_confidence"`
	AliasingRisk     bool     `json:"aliasing_risk,omitempty"`
	DominantPeriodMs int      `json:"dominant_period_ms,omitempty"`
}

// CompareDatasetResult mirrors fungen_compare.compare_dataset output.
type CompareDatasetResult struct {
	Rows     []CompareRow
	Excluded []map[string]string
	Skipped  [][2]string // path-or-name, reason
}

// CompareDataset walks datasetDir for FunGen references and SamNPlayer
// *__hub/tf/tj.funscript batch outputs, then runs BestLagCorrelation per pair.
// Thin Go port of generator/fungen_compare.compare_dataset (compare half only).
func CompareDataset(datasetDir string, maxLagMs int) (CompareDatasetResult, error) {
	if maxLagMs <= 0 {
		maxLagMs = DefaultMaxLagMs
	}
	stems, err := collectBatchStems(datasetDir)
	if err != nil {
		return CompareDatasetResult{}, err
	}
	refs, skipped, err := collectReferences(datasetDir)
	if err != nil {
		return CompareDatasetResult{}, err
	}

	var rows []CompareRow
	var excluded []map[string]string
	sort.Slice(refs, func(i, j int) bool {
		return strings.ToLower(filepath.Base(refs[i])) < strings.ToLower(filepath.Base(refs[j]))
	})
	for _, refPath := range refs {
		refScript, err := Load(refPath)
		if err != nil {
			skipped = append(skipped, [2]string{filepath.Base(refPath), err.Error()})
			continue
		}
		stem, reason := matchBatchStem(refPath, stems)
		if stem == "" {
			skipped = append(skipped, [2]string{filepath.Base(refPath), reason})
			continue
		}
		for _, kind := range []string{"hub", "tf", "tj"} {
			variantPath, ok := stems[stem][kind]
			if !ok {
				continue
			}
			variant, err := Load(variantPath)
			if err != nil {
				skipped = append(skipped, [2]string{filepath.Base(variantPath), err.Error()})
				continue
			}
			corr := BestLagCorrelation(refScript.Actions, variant.Actions, maxLagMs, DefaultLagStepMs, DefaultResampleStepMs)
			if corr == nil {
				excluded = append(excluded, map[string]string{
					"reference": filepath.Base(refPath),
					"kind":      kind,
					"reason":    "undefined_correlation (constant series or no overlap)",
				})
				continue
			}
			rows = append(rows, CompareRow{
				Reference:        filepath.Base(refPath),
				Stem:             stem,
				Kind:             kind,
				R:                corr.R,
				LagMs:            corr.LagMs,
				Orientation:      corr.Orientation,
				NSamples:         corr.NSamples,
				ShapeError:       corr.ShapeError,
				RZeroLag:         corr.RZeroLag,
				LowConfidence:    corr.LowConfidence,
				AliasingRisk:     corr.AliasingRisk,
				DominantPeriodMs: corr.DominantPeriodMs,
			})
		}
	}
	return CompareDatasetResult{Rows: rows, Excluded: excluded, Skipped: skipped}, nil
}

// FormatCompareReport builds the markdown report (fungen_compare.format_report).
func FormatCompareReport(result CompareDatasetResult) string {
	var b strings.Builder
	b.WriteString("# FunGen reference comparison (Go funscript.CompareDataset)\n\n")
	fmt.Fprintf(&b, "Compared rows: %d · excluded (undefined): %d · skipped (unreadable/unmatched): %d\n\n",
		len(result.Rows), len(result.Excluded), len(result.Skipped))
	for _, row := range result.Rows {
		lagNote := "0ms"
		if row.LagMs != 0 {
			lagNote = fmt.Sprintf("%+dms", row.LagMs)
		}
		orient := ""
		if row.Orientation == "inverted" {
			orient = " INVERTED"
		}
		shape := "n/a"
		if row.ShapeError != nil {
			shape = fmt.Sprintf("%.3f", *row.ShapeError)
		}
		zero := "n/a"
		if row.RZeroLag != nil {
			zero = fmt.Sprintf("%.3f", *row.RZeroLag)
		}
		low := ""
		if row.LowConfidence {
			low = " [LOW CONFIDENCE: short overlap]"
		}
		alias := ""
		if row.AliasingRisk {
			alias = fmt.Sprintf(" [ALIASING RISK period≈%dms]", row.DominantPeriodMs)
		}
		fmt.Fprintf(&b, "- %s vs **%s**: r=%.3f @ lag %s%s (r at zero-lag/normal: %s) shape_err=%s n=%d%s%s\n",
			row.Reference, row.Kind, row.R, lagNote, orient, zero, shape, row.NSamples, low, alias)
	}
	if len(result.Excluded) > 0 {
		b.WriteString("\n## Excluded (undefined correlation, not counted in any mean)\n")
		for _, item := range result.Excluded {
			fmt.Fprintf(&b, "- %s vs %s: %s\n", item["reference"], item["kind"], item["reason"])
		}
	}
	if len(result.Skipped) > 0 {
		b.WriteString("\n## Skipped\n")
		for _, item := range result.Skipped {
			fmt.Fprintf(&b, "- %s: %s\n", item[0], item[1])
		}
	}
	byKind := map[string][]float64{}
	confident := map[string][]float64{}
	for _, row := range result.Rows {
		byKind[row.Kind] = append(byKind[row.Kind], row.R)
		if !row.LowConfidence {
			confident[row.Kind] = append(confident[row.Kind], row.R)
		}
	}
	if len(byKind) > 0 {
		b.WriteString("\n## Summary\n")
		kinds := make([]string, 0, len(byKind))
		for k := range byKind {
			kinds = append(kinds, k)
		}
		sort.Strings(kinds)
		for _, kind := range kinds {
			values := byKind[kind]
			fmt.Fprintf(&b, "- mean r (%s, all, n=%d): %.3f\n", kind, len(values), mean(values))
			cvals := confident[kind]
			if len(cvals) > 0 {
				fmt.Fprintf(&b, "  - excluding low-confidence rows (n=%d): %.3f\n", len(cvals), mean(cvals))
			} else {
				b.WriteString("  - no rows above the low-confidence sample threshold\n")
			}
		}
	}
	return b.String()
}

func contentHash(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:]), nil
}

func dedupeByContent(paths []string) ([]string, error) {
	seen := map[string]bool{}
	var unique []string
	for _, p := range paths {
		h, err := contentHash(p)
		if err != nil {
			continue
		}
		if seen[h] {
			continue
		}
		seen[h] = true
		unique = append(unique, p)
	}
	return unique, nil
}

func collectBatchStems(datasetDir string) (map[string]map[string]string, error) {
	var variantPaths []string
	err := filepath.WalkDir(datasetDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		if batchSuffixRE.MatchString(d.Name()) {
			variantPaths = append(variantPaths, path)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	variantPaths, err = dedupeByContent(variantPaths)
	if err != nil {
		return nil, err
	}
	stems := map[string]map[string]string{}
	for _, path := range variantPaths {
		name := filepath.Base(path)
		m := batchSuffixRE.FindStringSubmatch(name)
		if m == nil {
			continue
		}
		stem := name[:len(name)-len(m[0])]
		if stems[stem] == nil {
			stems[stem] = map[string]string{}
		}
		stems[stem][m[1]] = path
	}
	return stems, nil
}

func collectReferences(datasetDir string) (refs []string, skipped [][2]string, err error) {
	err = filepath.WalkDir(datasetDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		name := d.Name()
		if strings.HasSuffix(strings.ToLower(name), ".fungen") {
			skipped = append(skipped, [2]string{name, "fungen_binary_not_read"})
			return nil
		}
		if strings.HasSuffix(strings.ToLower(name), ".funscript") && !batchSuffixRE.MatchString(name) {
			refs = append(refs, path)
		}
		return nil
	})
	if err != nil {
		return nil, skipped, err
	}
	refs, err = dedupeByContent(refs)
	return refs, skipped, err
}

func matchBatchStem(refPath string, stems map[string]map[string]string) (stem, reason string) {
	base := strings.TrimSuffix(filepath.Base(refPath), filepath.Ext(refPath))
	if _, ok := stems[base]; ok {
		return base, ""
	}
	short := strings.TrimSpace(prefix80(base))
	var candidates []string
	for s := range stems {
		if strings.TrimSpace(prefix80(s)) == short {
			candidates = append(candidates, s)
		}
	}
	sort.Strings(candidates)
	if len(candidates) == 1 {
		return candidates[0], ""
	}
	if len(candidates) > 1 {
		return "", "ambiguous_match:" + strings.Join(candidates, ",")
	}
	return "", "no_match"
}

func prefix80(s string) string {
	if len(s) <= 80 {
		return s
	}
	return s[:80]
}
