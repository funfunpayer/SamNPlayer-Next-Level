package generator

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/funfunpayer/SamNPlayer/funscript"
	"github.com/funfunpayer/SamNPlayer/samn"
)

// ReviewAutoContactCandidate confirms or rejects one author:auto scene-map
// mark in the companion .samn beside videoOrSamn.
//
//	accept=true  → Reviewed=true (eligible for P5c YOLO export)
//	accept=false → mark deleted
//
// Other marks are untouched. Everyday CSRT / Generate defaults unchanged.
func ReviewAutoContactCandidate(videoOrSamn, markID string, accept bool) error {
	markID = strings.TrimSpace(markID)
	if markID == "" {
		return fmt.Errorf("generator: empty mark id")
	}
	samnPath, err := resolveSamnPath(videoOrSamn)
	if err != nil {
		return err
	}
	doc, err := samn.Load(samnPath)
	if err != nil {
		return err
	}
	if doc.SceneMap == nil || len(doc.SceneMap.Marks) == 0 {
		return fmt.Errorf("generator: %s has no scene-map marks", samnPath)
	}
	found := false
	kept := make([]funscript.SceneMapMark, 0, len(doc.SceneMap.Marks))
	for _, m := range doc.SceneMap.Marks {
		if m.ID != markID {
			kept = append(kept, m)
			continue
		}
		found = true
		if !strings.EqualFold(m.Author, "auto") {
			return fmt.Errorf("generator: mark %q is author %q (only author:auto is reviewable here)", markID, m.Author)
		}
		if accept {
			yes := true
			m.Reviewed = &yes
			kept = append(kept, m)
		}
		// reject: drop the mark
	}
	if !found {
		return fmt.Errorf("generator: mark %q not found in %s", markID, samnPath)
	}
	doc.SceneMap.Marks = kept
	return samn.Save(samnPath, doc)
}

// ImportContactCandidatesForVideo runs ImportContactCandidates on the
// companion .samn beside videoPath (must already have a scene map with size).
func ImportContactCandidatesForVideo(videoPath, contactPath string) (int, error) {
	samnPath, err := resolveSamnPath(videoPath)
	if err != nil {
		return 0, err
	}
	return ImportContactCandidates(samnPath, contactPath, ContactCandidateOptions{})
}

func resolveSamnPath(videoOrSamn string) (string, error) {
	videoOrSamn = strings.TrimSpace(videoOrSamn)
	if videoOrSamn == "" {
		return "", fmt.Errorf("generator: empty path")
	}
	if strings.EqualFold(filepath.Ext(videoOrSamn), ".samn") {
		if st, err := os.Stat(videoOrSamn); err != nil || st.IsDir() {
			return "", fmt.Errorf("generator: .samn not found: %s", videoOrSamn)
		}
		return videoOrSamn, nil
	}
	p := samn.CompanionSamnPath(videoOrSamn)
	if st, err := os.Stat(p); err != nil || st.IsDir() {
		return "", fmt.Errorf("generator: companion .samn missing — Create with rhythm grid first (%s)", p)
	}
	return p, nil
}
