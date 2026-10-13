package funscript

import (
	"fmt"
	"strings"
)

// ChaptersFromAudioSegments maps Speech-Hold / Feel taxonomy segments into
// chapter marks for Review. Labels stay English product copy. Empty or
// invalid spans are skipped. Does not invent stroke actions.
func ChaptersFromAudioSegments(segs []AudioSegmentHint) []ChapterMark {
	if len(segs) == 0 {
		return nil
	}
	out := make([]ChapterMark, 0, len(segs))
	for _, s := range segs {
		start, end := s.StartMs, s.EndMs
		if end < start {
			start, end = end, start
		}
		if end <= start {
			continue
		}
		name := chapterNameForAudioSegment(s)
		out = append(out, ChapterMark{
			Name:      name,
			StartTime: start,
			EndTime:   end,
		})
	}
	return out
}

func chapterNameForAudioSegment(s AudioSegmentHint) string {
	label := strings.ToLower(strings.TrimSpace(s.Label))
	switch label {
	case "holding":
		if s.SpeechHold {
			return "Hold (speech)"
		}
		return "Hold"
	case "gentle":
		return "Gentle"
	case "intense":
		return "Intense"
	case "climax":
		return "Climax"
	}
	if label != "" {
		return strings.ToUpper(label[:1]) + label[1:]
	}
	if s.SpeechHold {
		return "Hold (speech)"
	}
	return fmt.Sprintf("Segment %ds", s.StartMs/1000)
}
