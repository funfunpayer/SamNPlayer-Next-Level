//go:build cgo && opencv

package generator

import "github.com/funfunpayer/SamNPlayer/generator/trackcv"

// ScanSceneMap runs a quick rhythm heatmap without CSRT (explicit Advanced
// trigger — Owner decision 24 Sep). n <= 0 uses DefaultSceneMapWindows.
func ScanSceneMap(videoPath string, n int) (SceneMapDTO, error) {
	if n <= 0 {
		n = DefaultSceneMapWindows
	}
	m, err := trackcv.ScanSceneMap(videoPath, n)
	if err != nil {
		return SceneMapDTO{}, err
	}
	return sceneMapFromTrackcv(m), nil
}

func sceneMapFromTrackcv(m trackcv.SceneMap) SceneMapDTO {
	out := SceneMapDTO{
		Version: m.Version,
		Cols:    m.Cols,
		Rows:    m.Rows,
		Width:   m.Width,
		Height:  m.Height,
		Windows: make([]MapWindowDTO, len(m.Windows)),
	}
	for i, w := range m.Windows {
		out.Windows[i] = MapWindowDTO{
			StartMs:    w.StartMs,
			EndMs:      w.EndMs,
			TempoHz:    w.TempoHz,
			Score:      append([]uint8(nil), w.Score...),
			ChosenCell: w.ChosenCell,
			BoxCX:      w.BoxCX,
			BoxCY:      w.BoxCY,
			SignRule:   w.SignRule,
			TrackerR:   w.TrackerR,
			Marks:      append([]string(nil), w.Marks...),
		}
	}
	return out
}
