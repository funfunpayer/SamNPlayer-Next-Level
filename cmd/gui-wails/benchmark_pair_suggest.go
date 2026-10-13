package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

// BenchmarkPairSuggestion is a best-effort FunGen-ref + Everyday-candidate
// pairing beside a short clip (Clip-Prep → Bench Compare). Paths stay local;
// Everyday Go CSRT remains the production basis.
type BenchmarkPairSuggestion struct {
	Video      string   `json:"video"`
	Reference  string   `json:"reference,omitempty"`
	Candidate  string   `json:"candidate,omitempty"`
	References []string `json:"references,omitempty"`
	Candidates []string `json:"candidates,omitempty"`
	Note       string   `json:"note,omitempty"`
}

// FunGen/dataset convention: plain stem.funscript = reference; stem__hub.funscript
// (and other axis suffixes) = SamNPlayer Everyday candidate. See
// generator/testdata/golden_clips/*/README.md.
var axisFunscriptSuffix = regexp.MustCompile(`(?i)__(hub|tf|tj|roll|pitch|surge|sway|twist)\.funscript$`)

// SuggestBenchmarkPairBesideVideo looks next to a clip (and one level of
// subdirs like mit_yolo/) for related .funscript files and picks a Ref +
// Candidate pair using golden naming. No scoring — Owner still hits Score.
func (a *App) SuggestBenchmarkPairBesideVideo(videoPath string) (BenchmarkPairSuggestion, error) {
	return suggestBenchmarkPairBesideVideo(videoPath)
}

func suggestBenchmarkPairBesideVideo(videoPath string) (BenchmarkPairSuggestion, error) {
	out := BenchmarkPairSuggestion{}
	videoPath = strings.TrimSpace(videoPath)
	if videoPath == "" {
		return out, fmt.Errorf("video path required")
	}
	clean := filepath.Clean(videoPath)
	out.Video = clean

	info, err := os.Stat(clean)
	if err != nil {
		return out, err
	}
	if info.IsDir() {
		return out, fmt.Errorf("expected a video file, got directory")
	}

	stem := strings.TrimSuffix(filepath.Base(clean), filepath.Ext(clean))
	if stem == "" {
		return out, fmt.Errorf("empty video stem")
	}

	found, err := collectRelatedFunscripts(filepath.Dir(clean), stem)
	if err != nil {
		return out, err
	}
	if len(found) == 0 {
		out.Note = "No related .funscript beside this clip — put FunGen ref + Everyday candidate next to the video (or in a subfolder)."
		return out, nil
	}

	var refs, cands []string
	for _, p := range found {
		base := filepath.Base(p)
		if isEverydayAxisFunscript(base) {
			cands = append(cands, p)
		} else {
			refs = append(refs, p)
		}
	}
	sort.Strings(refs)
	sort.Strings(cands)
	out.References = refs
	out.Candidates = cands

	exactRef := filepath.Join(filepath.Dir(clean), stem+".funscript")
	exactHub := filepath.Join(filepath.Dir(clean), stem+"__hub.funscript")

	out.Reference = pickPreferred(refs, exactRef, stem+".funscript")
	out.Candidate = pickPreferred(cands, exactHub, stem+"__hub.funscript")

	switch {
	case out.Reference != "" && out.Candidate != "":
		out.Note = "Paired FunGen/ref + Everyday (__hub) beside the clip."
		if len(refs) > 1 {
			out.Note += fmt.Sprintf(" %d reference options — refine if needed.", len(refs))
		}
	case out.Reference != "" && out.Candidate == "":
		// Everyday Create writes stem.funscript by default. If only that exists,
		// treat it as the candidate and leave reference empty for FunGen pick.
		if len(refs) == 1 && filepath.Base(out.Reference) == stem+".funscript" && len(cands) == 0 {
			out.Candidate = out.Reference
			out.Reference = ""
			out.Candidates = []string{out.Candidate}
			out.References = nil
			out.Note = "Only stem.funscript found (Everyday default path) — filled as candidate. Browse FunGen/reference separately."
		} else {
			out.Note = "Reference found; Everyday candidate (__hub / axis) missing — Create on this clip or browse candidate."
		}
	case out.Reference == "" && out.Candidate != "":
		out.Note = "Everyday candidate found; FunGen/reference missing — browse reference."
	default:
		out.Note = "Related scripts found but could not classify — browse Reference and Candidate."
	}
	return out, nil
}

func isEverydayAxisFunscript(base string) bool {
	return axisFunscriptSuffix.MatchString(base)
}

func pickPreferred(paths []string, exactPath string, preferredBase string) string {
	if len(paths) == 0 {
		return ""
	}
	for _, p := range paths {
		if filepath.Clean(p) == filepath.Clean(exactPath) {
			return p
		}
	}
	for _, p := range paths {
		if strings.EqualFold(filepath.Base(p), preferredBase) {
			return p
		}
	}
	return paths[0]
}

// collectRelatedFunscripts lists .funscript files in dir and immediate
// subdirs whose basename starts with stem (case-insensitive), matching
// golden layout (mit_yolo/stem.funscript, stem__hub.funscript, …).
func collectRelatedFunscripts(dir, stem string) ([]string, error) {
	stemLower := strings.ToLower(stem)
	var out []string
	seen := map[string]bool{}

	addIfRelated := func(path string) {
		base := filepath.Base(path)
		if !strings.HasSuffix(strings.ToLower(base), ".funscript") {
			return
		}
		name := strings.TrimSuffix(base, filepath.Ext(base))
		nl := strings.ToLower(name)
		if nl != stemLower && !strings.HasPrefix(nl, stemLower+"_") && !strings.HasPrefix(nl, stemLower+"__") {
			// Allow stem_ohne_yolo.funscript style (prefix stem + underscore)
			if !strings.HasPrefix(nl, stemLower) {
				return
			}
		}
		clean := filepath.Clean(path)
		if seen[clean] {
			return
		}
		seen[clean] = true
		out = append(out, clean)
	}

	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	for _, e := range entries {
		name := e.Name()
		full := filepath.Join(dir, name)
		if e.IsDir() {
			subEntries, err := os.ReadDir(full)
			if err != nil {
				continue
			}
			for _, se := range subEntries {
				if se.IsDir() {
					continue
				}
				addIfRelated(filepath.Join(full, se.Name()))
			}
			continue
		}
		addIfRelated(full)
	}
	sort.Strings(out)
	return out, nil
}
