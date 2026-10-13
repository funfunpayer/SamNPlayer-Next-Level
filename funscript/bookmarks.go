package funscript

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
)

// Bookmark is a named time marker in funscript metadata (OFS/community style).
type Bookmark struct {
	Name string `json:"name"`
	Time int64  `json:"time"` // ms
}

// ChapterMark is a named span in metadata (optional end for open-ended).
type ChapterMark struct {
	Name      string `json:"name"`
	StartTime int64  `json:"startTime"`
	EndTime   int64  `json:"endTime,omitempty"`
}

// flexTimeMs unmarshals OFS-style times: int ms, float ms, or seconds
// (small floats like 12.5 → 12500 when clearly seconds).
type flexTimeMs int64

func (t *flexTimeMs) UnmarshalJSON(b []byte) error {
	b = bytesTrimSpace(b)
	if len(b) == 0 || string(b) == "null" {
		*t = 0
		return nil
	}
	// Quoted number
	if b[0] == '"' {
		var s string
		if err := json.Unmarshal(b, &s); err != nil {
			return err
		}
		v, err := strconv.ParseFloat(s, 64)
		if err != nil {
			// OFS 3 writes chapter/bookmark times as "HH:MM:SS.mmm".
			ms, ok := parseClockMs(s)
			if !ok {
				return err
			}
			*t = flexTimeMs(ms)
			return nil
		}
		*t = flexTimeMs(normalizeTimeToMs(v))
		return nil
	}
	var f float64
	if err := json.Unmarshal(b, &f); err != nil {
		return err
	}
	*t = flexTimeMs(normalizeTimeToMs(f))
	return nil
}

// parseClockMs reads "[[H:]M:]S[.fff]" ("00:01:23.456", "1:23.5") as ms.
func parseClockMs(s string) (int64, bool) {
	parts := strings.Split(strings.TrimSpace(s), ":")
	if len(parts) < 2 || len(parts) > 3 {
		return 0, false
	}
	var total float64
	for i, p := range parts {
		v, err := strconv.ParseFloat(p, 64)
		if err != nil || v < 0 || math.IsNaN(v) || math.IsInf(v, 0) {
			return 0, false
		}
		if i < len(parts)-1 && v != math.Trunc(v) {
			return 0, false // only the seconds part may have a fraction
		}
		total = total*60 + v
	}
	return int64(math.Round(total * 1000)), true
}

func bytesTrimSpace(b []byte) []byte {
	i, j := 0, len(b)
	for i < j && (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r') {
		i++
	}
	for j > i && (b[j-1] == ' ' || b[j-1] == '\t' || b[j-1] == '\n' || b[j-1] == '\r') {
		j--
	}
	return b[i:j]
}

// normalizeTimeToMs: values under 1e5 with a fractional part look like
// seconds (OFS often uses 12.5); large integers stay ms.
func normalizeTimeToMs(v float64) int64 {
	if math.IsNaN(v) || math.IsInf(v, 0) {
		return 0
	}
	abs := math.Abs(v)
	_, frac := math.Modf(abs)
	if abs < 100000 && frac > 1e-9 {
		return int64(math.Round(v * 1000))
	}
	return int64(math.Round(v))
}

type bookmarkWire struct {
	Name string     `json:"name"`
	Time flexTimeMs `json:"time"`
}

type chapterWire struct {
	Name      string     `json:"name"`
	StartTime flexTimeMs `json:"startTime"`
	EndTime   flexTimeMs `json:"endTime"`
}

// LoadBookmarks reads metadata.bookmarks without rewriting the file.
func LoadBookmarks(path string) ([]Bookmark, error) {
	meta, err := readMetadataMap(path)
	if err != nil {
		return nil, err
	}
	raw, ok := meta["bookmarks"]
	if !ok || len(raw) == 0 || string(raw) == "null" {
		return nil, nil
	}
	var wire []bookmarkWire
	if err := json.Unmarshal(raw, &wire); err != nil {
		return nil, fmt.Errorf("funscript: bookmarks ungültig: %w", err)
	}
	out := make([]Bookmark, len(wire))
	for i, w := range wire {
		out[i] = Bookmark{Name: w.Name, Time: int64(w.Time)}
	}
	return out, nil
}

// SaveBookmarks replaces metadata.bookmarks, preserving other metadata keys.
func SaveBookmarks(path string, bookmarks []Bookmark) error {
	sorted := append([]Bookmark(nil), bookmarks...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].Time < sorted[j].Time })
	return patchMetadataKey(path, "bookmarks", sorted)
}

// LoadChapters reads metadata.chapters.
func LoadChapters(path string) ([]ChapterMark, error) {
	meta, err := readMetadataMap(path)
	if err != nil {
		return nil, err
	}
	raw, ok := meta["chapters"]
	if !ok || len(raw) == 0 || string(raw) == "null" {
		return nil, nil
	}
	var wire []chapterWire
	if err := json.Unmarshal(raw, &wire); err != nil {
		return nil, fmt.Errorf("funscript: chapters ungültig: %w", err)
	}
	out := make([]ChapterMark, len(wire))
	for i, w := range wire {
		out[i] = ChapterMark{
			Name:      w.Name,
			StartTime: int64(w.StartTime),
			EndTime:   int64(w.EndTime),
		}
	}
	return out, nil
}

// SaveChapters replaces metadata.chapters.
func SaveChapters(path string, chapters []ChapterMark) error {
	sorted := append([]ChapterMark(nil), chapters...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].StartTime < sorted[j].StartTime })
	return patchMetadataKey(path, "chapters", sorted)
}

func readMetadataMap(path string) (map[string]json.RawMessage, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return nil, fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	meta := map[string]json.RawMessage{}
	if raw, ok := doc["metadata"]; ok && len(raw) > 0 && string(raw) != "null" {
		if err := json.Unmarshal(raw, &meta); err != nil {
			return nil, fmt.Errorf("funscript: metadata ungültig: %w", err)
		}
	}
	return meta, nil
}

func patchMetadataKey(path, key string, value any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("funscript: Datei konnte nicht gelesen werden: %w", err)
	}
	var doc map[string]json.RawMessage
	if err := json.Unmarshal(data, &doc); err != nil {
		return fmt.Errorf("funscript: ungültiges JSON: %w", err)
	}
	meta := map[string]json.RawMessage{}
	if raw, ok := doc["metadata"]; ok && len(raw) > 0 && string(raw) != "null" {
		if err := json.Unmarshal(raw, &meta); err != nil {
			return fmt.Errorf("funscript: metadata ungültig: %w", err)
		}
	}
	encoded, err := json.Marshal(value)
	if err != nil {
		return err
	}
	meta[key] = encoded
	metaRaw, err := json.Marshal(meta)
	if err != nil {
		return err
	}
	doc["metadata"] = metaRaw
	out, err := json.Marshal(doc)
	if err != nil {
		return err
	}
	return writeFileAtomic(path, out, 0o644)
}
