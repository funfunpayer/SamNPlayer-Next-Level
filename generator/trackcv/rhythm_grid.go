package trackcv

import (
	"math"
	"sort"
)

// Rhythm grid: an alternative source for the stroke signal that tolerates
// CSRT drift.
//
// CSRT on long clips walks off the target gradually (clip_voll: by ~140s the
// box sits ~110px beside the shaft), and the stroke curve is the box's own
// motion - so the curve degrades with it. The rhythm grid keeps CSRT only as
// a rough anchor: per frame the dense optical flow is averaged onto a coarse
// cell grid (FlowCells), and per 8s window the cell near the box whose
// motion is most concentrated at the stroke tempo supplies the signal. The
// box only has to stay within ~3 cells of the real action, not on it.
//
// Originally measured (per-window free cell choice, no identity lock)
// against both FunGen references through the production post pipeline
// (docs/AGENT_COORD.md, 23 Sep, "rhythm grid"): clip_voll windowed r
// 0.386/0.552 -> 0.411/0.767, clip_ausschnitt 0.449/0.712 -> 0.466/0.877.
// The per-shot identity lock (Manus/#248, 24 Sep) then cost part of that
// (clip_voll 0.427/0.626, clip_ausschnitt 0.383/0.666): it kept the cell
// nearest the ROI centre even after its rhythm died. With the lock release
// (rhythmLockReleaseFactor, 27 Sep): 0.455/0.752 and 0.486/0.887 - at or
// above the pre-lock numbers, #248's adjacent-thigh protection intact.
//
// Scene map (P1 / docs/SCENE_MAP_PLAN.md): the same scoring is exposed as a
// SceneMap so the GUI can show the heatmap and later honour user marks.
// scoreWindows produces the map; chooseAndStitch turns it into the curve.
// rhythmGridPositions is a thin bit-identical wrapper over both.
const (
	rhythmGridCols    = 16
	rhythmWindowSec   = 8.0
	rhythmStepSec     = 2.0
	rhythmSearchCells = 3.0 // search radius around the box, in cell widths
	rhythmTempoLoHz   = 0.6
	rhythmTempoHiHz   = 2.5
	rhythmBandHz      = 0.15
	rhythmTotalLoHz   = 0.2
	rhythmTotalHiHz   = 6.0
	rhythmTopCells    = 10
	rhythmAnchorSec   = 2.0 // detrend window for the CSRT sign reference
	// rhythmSignMinR: below this |r| between the cell and the tracker the
	// tracker's sign is a coin toss, so orientation comes from continuity
	// with the curve already written instead. clip_voll: 4 of 140 chunks
	// (all |r| <= 0.04), windowed r vs the YOLO reference 0.658 -> 0.766
	// and 10/10 windows consistently oriented. Raising it to >= 0.25 also
	// overrides still-informative tracker signs and measured worse.
	rhythmSignMinR = 0.1
	// Seed margin for first-cell identity (Manus #248 target lock).
	rhythmSeedMarginCells = 0.35
	// rhythmLockReleaseFactor: the identity lock is released when the locked
	// cell's rhythm score falls below 1/this of the strongest cell in the
	// search radius around the current box; that cell is then locked
	// instead. Seeding takes the cell nearest the ROI centre, which on real
	// clips is often on the breast/hand beside the stroke, and its rhythm
	// dies ~50x below the cleavage cell as the scene moves on. Real
	// clip_voll / clip_ausschnitt (real Go TrackROI): windowed r vs FunGen
	// 0.427/0.626 -> 0.455/0.752 and 0.383/0.666 -> 0.486/0.887, 12/12 windows oriented
	// with the YOLO reference (11/12 locked). An adjacent "thigh" at 3-4x
	// the target amplitude is only ~9-16x its score, so 30 keeps the #248
	// hijack protection (tests below); 10 already loses it. 20-50 measured
	// similar on the goldens (docs/AGENT_COORD.md, 27 Sep, IdLock).
	rhythmLockReleaseFactor = 30.0
	// rhythmSourceScoreFactor: prefer a source-hint cell unless its score
	// is below this × the best eligible cell outside the hint (M3).
	rhythmSourceScoreFactor = 0.5
)

// SceneMap is the per-window, per-cell rhythm heatmap (M1).
// Full runs fill ChosenCell / box / sign fields; a quick scan leaves them zero.
type SceneMap struct {
	Version       int
	Cols, Rows    int
	Width, Height int
	Windows       []MapWindow
}

// MapWindow is one 8s (nominal) scoring window on the grid.
type MapWindow struct {
	StartMs, EndMs int64
	TempoHz        float64
	// Score is Cols*Rows values normalized 0–255 (E²/total scaled to max).
	Score []uint8
	// ChosenCell is the cell used for the curve, or -1 if the window fell
	// back to the tracker. Only set on full runs (CSRT available).
	ChosenCell   int
	BoxCX, BoxCY float64 // CSRT box centre at window mid (full-run only)
	SignRule     string  // "tracker" | "continuity" (full-run only)
	TrackerR     float64 // |r| cell vs tracker (full-run only)
	// Marks lists active exclude/source mark IDs obeyed in this window (M3).
	Marks []string
	// scoreRaw holds the unnormalized float scores used by chooseAndStitch
	// so the curve path stays bit-identical to the pre-split code. Not
	// exported / not persisted.
	scoreRaw []float64
	// Frame indices of the window in the source cellV (for stitching).
	frameA, frameB, frameMid int
	hasScore                 bool
}

// rhythmSeed is the confirmed tip/ROI at shot start (user mark or Apply target).
// First rhythm cell is chosen inside this box; later windows keep that cell.
type rhythmSeed struct {
	X, Y, W, H float64
}

// rhythmGridRows keeps cells roughly square for the frame's aspect ratio.
func rhythmGridRows(width, height int) int {
	if width <= 0 || height <= 0 {
		return 9
	}
	return max(1, int(math.Round(float64(rhythmGridCols)*float64(height)/float64(width))))
}

// scoreWindows builds the SceneMap from per-frame cell flow.
// cellV[i] is the flow (px/frame, one axis) of every cell between frame i-1
// and i (all zero for frame 0 and scene cuts).
// When cx/cy are non-nil and length-matched, each window also records the
// box centre at the window midpoint (full-run path). When nil, this is a
// quick-scan map (no ChosenCell / box / sign).
func scoreWindows(cellV [][]float32, gw, gh int, width, height int,
	cx, cy []float64, fps float64) SceneMap {
	n := len(cellV)
	m := SceneMap{
		Version: 1,
		Cols:    gw,
		Rows:    gh,
		Width:   width,
		Height:  height,
	}
	if n == 0 || fps <= 0 || gw < 1 || gh < 1 {
		return m
	}
	cells := gw * gh
	win := int(math.Round(rhythmWindowSec * fps))
	step := int(math.Round(rhythmStepSec * fps))
	if win < 8 || step < 1 {
		return m
	}
	haveBox := len(cx) == n && len(cy) == n
	x := make([][]float64, cells) // per-cell window, reused
	for s := -win/2 + step/2; s < n; s += step {
		a, b := max(0, s), min(n, s+win)
		mlen := b - a
		if mlen < win/2 {
			continue
		}
		for c := 0; c < cells; c++ {
			if cap(x[c]) < mlen {
				x[c] = make([]float64, mlen)
			}
			x[c] = x[c][:mlen]
			acc := 0.0
			for i := 0; i < mlen; i++ {
				acc += float64(cellV[a+i][c])
				x[c][i] = acc
			}
			detrendLinear(x[c])
		}
		score, tempo := rhythmScoresWithTempo(x, fps)
		if score == nil {
			continue
		}
		mid := min(n-1, s+win/2)
		w := MapWindow{
			StartMs:    int64(float64(a) * 1000.0 / fps),
			EndMs:      int64(float64(b) * 1000.0 / fps),
			TempoHz:    tempo,
			Score:      normalizeScores(score),
			scoreRaw:   score,
			frameA:     a,
			frameB:     b,
			frameMid:   mid,
			hasScore:   true,
			ChosenCell: -1,
		}
		if haveBox {
			w.BoxCX, w.BoxCY = cx[mid], cy[mid]
		}
		m.Windows = append(m.Windows, w)
	}
	return m
}

// normalizeScores maps float scores to 0–255 relative to the window max.
func normalizeScores(score []float64) []uint8 {
	out := make([]uint8, len(score))
	maxS := 0.0
	for _, v := range score {
		if v > maxS {
			maxS = v
		}
	}
	if maxS <= 0 {
		return out
	}
	for i, v := range score {
		s := v / maxS * 255
		if s > 255 {
			s = 255
		}
		if s < 0 {
			s = 0
		}
		out[i] = uint8(s + 0.5)
	}
	return out
}

// chooseAndStitch picks per-window cells under target-lock rules and writes
// the stroke curve. Locked identity: first cell is seeded from the confirmed
// ROI; later windows keep that cell even if a neighbour scores higher.
// Scene cuts re-seed from the re-anchored tracker and keep the first
// half-window on CSRT. Sign reference is segmented at cuts.
// marks (M3): exclude skips cells; source prefers hint cells when score
// ≥ 0.5× best outside; region has no engine effect. Nil marks = bit-identical
// to the pre-M3 path. Identity lock still wins over source hints unless the
// locked cell is excluded (then re-seed).
func chooseAndStitch(m *SceneMap, cellV [][]float32, gw, gh int, width, height int,
	cx, cy, anchor []float64, seed rhythmSeed, sceneCuts []int, fps float64,
	marks []SceneMark) []float64 {
	n := len(cellV)
	if n == 0 || len(cx) != n || len(cy) != n || len(anchor) != n || fps <= 0 {
		return append([]float64(nil), anchor...)
	}
	cellW := float64(width) / float64(gw)
	cellH := float64(height) / float64(gh)
	radius := rhythmSearchCells * cellW

	signRef := rhythmSegmentedSignReference(
		anchor, sceneCuts, int(math.Round(rhythmAnchorSec*fps)))

	v := make([]float64, n)
	for i := 1; i < n; i++ {
		v[i] = anchor[i] - anchor[i-1]
	}
	for _, cut := range sceneCuts {
		if cut > 0 && cut < n {
			v[cut] = 0
		}
	}

	win := int(math.Round(rhythmWindowSec * fps))
	step := int(math.Round(rhythmStepSec * fps))
	if win < 8 || step < 1 {
		return append([]float64(nil), anchor...)
	}

	written := 0
	lastBest := -1
	activeSeed := seed
	activeSceneStart := 0
	for wi := range m.Windows {
		w := &m.Windows[wi]
		if !w.hasScore || w.scoreRaw == nil {
			continue
		}
		a, b, mid := w.frameA, w.frameB, w.frameMid
		sceneStart, sceneEnd := rhythmSceneBounds(sceneCuts, mid, n)
		if sceneStart != activeSceneStart {
			idx := min(n-1, max(0, sceneStart))
			activeSeed.X = cx[idx] - seed.W/2
			activeSeed.Y = cy[idx] - seed.H/2
			activeSceneStart = sceneStart
			lastBest = -1
			written = sceneStart
		}
		// Clip window to the active shot for sign / write bounds.
		a = max(a, sceneStart)
		b = min(b, sceneEnd)
		if b-a < win/2 {
			continue
		}
		midMs := (w.StartMs + w.EndMs) / 2
		excludes, sources, obeyed := activeMarkFilter(marks, midMs)
		w.Marks = obeyed

		score := w.scoreRaw
		cellExcluded := func(c int) bool {
			if len(excludes) == 0 {
				return false
			}
			px := (float64(c%gw) + 0.5) * cellW
			py := (float64(c/gw) + 0.5) * cellH
			for _, r := range excludes {
				if pointInRect(px, py, r) {
					return true
				}
			}
			return false
		}
		// Locked identity punched out → re-seed this window.
		if lastBest >= 0 && cellExcluded(lastBest) {
			lastBest = -1
		}

		best, bestS := -1, 0.0
		var cands []rhythmCand
		for c := 0; c < len(score); c++ {
			px := (float64(c%gw) + 0.5) * cellW
			py := (float64(c/gw) + 0.5) * cellH
			if cellExcluded(c) {
				continue
			}
			if lastBest < 0 {
				eligible := rhythmCellInSeed(px, py, activeSeed, cellW, cellH)
				if activeSeed.W <= 0 || activeSeed.H <= 0 {
					eligible = math.Hypot(px-cx[mid], py-cy[mid]) <= radius
				}
				if !eligible || score[c] <= 0 {
					continue
				}
				seedX := activeSeed.X + activeSeed.W/2
				seedY := activeSeed.Y + activeSeed.H/2
				if activeSeed.W <= 0 || activeSeed.H <= 0 {
					seedX, seedY = cx[mid], cy[mid]
				}
				distance := math.Hypot(px-seedX, py-seedY)
				inHint := false
				for _, r := range sources {
					if pointInRect(px, py, r) {
						inHint = true
						break
					}
				}
				cands = append(cands, rhythmCand{c: c, s: score[c], dist: distance, inH: inHint})
				continue
			}
			// Keep identity: do not hand off to a stronger neighbour.
			// Source hints do not override a locked cell (see M3 caveat).
			if c == lastBest && score[c] > bestS {
				best, bestS = c, score[c]
			}
		}
		if lastBest < 0 {
			best = pickRhythmCandidate(cands, len(sources) > 0)
			if best >= 0 {
				bestS = score[best]
			}
		} else if top := strongestCellInRadius(score, gw, cellW, cellH, radius,
			cx[mid], cy[mid], cellExcluded); top >= 0 &&
			(best < 0 || score[top] > rhythmLockReleaseFactor*score[best]) {
			// Locked cell went (nearly) silent: hand identity to the strongest
			// cell near the box. best < 0 = locked cell scored 0 this window.
			best, bestS = top, score[top]
		}
		w.ChosenCell = best
		if best < 0 {
			continue
		}
		lastBest = best
		mlen := b - a
		cellSeries := make([]float64, mlen)
		acc := 0.0
		for i := 0; i < mlen; i++ {
			acc += float64(cellV[a+i][best])
			cellSeries[i] = acc
		}
		detrendLinear(cellSeries)

		r := pearson(cellSeries, signRef[a:b])
		signRule := "tracker"
		if math.Abs(r) < rhythmSignMinR && written-a > step {
			r = continuityR(cellV, best, v, a, written)
			signRule = "continuity"
		}
		w.SignRule = signRule
		w.TrackerR = r
		sgn := 1.0
		if r < 0 {
			sgn = -1
		}
		gridWriteStart := sceneStart
		if sceneStart > 0 {
			gridWriteStart += win / 2
		}
		c0 := max(1, max(gridWriteStart, mid-step/2))
		c1 := min(n, min(sceneEnd, mid+step/2))
		for i := c0; i < c1; i++ {
			v[i] = sgn * float64(cellV[i][best])
		}
		written = c1
	}

	cutAt := make([]bool, n)
	for _, cut := range sceneCuts {
		if cut > 0 && cut < n {
			cutAt[cut] = true
		}
	}
	out := make([]float64, n)
	out[0] = anchor[0]
	for i := 1; i < n; i++ {
		if cutAt[i] {
			out[i] = anchor[i]
			continue
		}
		out[i] = out[i-1] + v[i]
	}
	return out
}

// strongestCellInRadius returns the highest-scoring non-excluded cell whose
// centre lies within radius of (bx, by), or -1.
func strongestCellInRadius(score []float64, gw int, cellW, cellH, radius, bx, by float64,
	excluded func(int) bool) int {
	best, bestS := -1, 0.0
	for c, s := range score {
		if s <= bestS || excluded(c) {
			continue
		}
		px := (float64(c%gw) + 0.5) * cellW
		py := (float64(c/gw) + 0.5) * cellH
		if math.Hypot(px-bx, py-by) <= radius {
			best, bestS = c, s
		}
	}
	return best
}

// activeMarkFilter returns exclude/source rects active at midMs and the IDs
// of marks the engine obeys (exclude + source only; region is label-only).
// Rects come from Follow Path when present (sceneMarkRectAt).
func activeMarkFilter(marks []SceneMark, midMs int64) (excludes, sources []Rect, ids []string) {
	for _, mk := range marks {
		if !sceneMarkActive(mk, midMs) {
			continue
		}
		r := sceneMarkRectAt(mk, midMs)
		switch mk.Kind {
		case "exclude":
			excludes = append(excludes, r)
			if mk.ID != "" {
				ids = append(ids, mk.ID)
			}
		case "source":
			sources = append(sources, r)
			if mk.ID != "" {
				ids = append(ids, mk.ID)
			}
		}
	}
	return excludes, sources, ids
}

// pickRhythmCandidate chooses among seed/radius-eligible cells. With source
// hints overlapping any candidate, prefer the best-scoring cell inside a
// hint unless its score is below rhythmSourceScoreFactor × best outside.
// Without overlapping hints, keep the pre-M3 seed-distance rule.
func pickRhythmCandidate(cands []rhythmCand, haveSources bool) int {
	if len(cands) == 0 {
		return -1
	}
	anyIn := false
	if haveSources {
		for _, c := range cands {
			if c.inH {
				anyIn = true
				break
			}
		}
	}
	if anyIn {
		bestIn, bestOut := -1, -1
		var inS, outS float64
		for _, c := range cands {
			if c.inH {
				if bestIn < 0 || c.s > inS {
					bestIn, inS = c.c, c.s
				}
			} else if bestOut < 0 || c.s > outS {
				bestOut, outS = c.c, c.s
			}
		}
		if bestIn >= 0 && (bestOut < 0 || inS >= rhythmSourceScoreFactor*outS) {
			return bestIn
		}
		if bestOut >= 0 {
			return bestOut
		}
		return bestIn
	}
	best, bestS, bestDist := -1, 0.0, math.Inf(1)
	for _, c := range cands {
		if c.dist < bestDist || (c.dist == bestDist && c.s > bestS) {
			best, bestS, bestDist = c.c, c.s, c.dist
		}
	}
	return best
}

type rhythmCand struct {
	c    int
	s    float64
	dist float64
	inH  bool
}

// rhythmGridPositions builds a position curve from per-frame cell flow.
// Implemented as scoreWindows + chooseAndStitch so the same numbers feed
// both the curve and the SceneMap (bit-identical to the pre-split code).
func rhythmGridPositions(cellV [][]float32, gw, gh int, width, height int,
	cx, cy, anchor []float64, seed rhythmSeed, sceneCuts []int, fps float64) []float64 {
	return rhythmGridPositionsMarks(cellV, gw, gh, width, height, cx, cy, anchor, seed, sceneCuts, fps, nil)
}

// rhythmContactHoldMs: default time window around a ContactPoint in which it
// counts for a frame (teachers sample every ~0.5 s; 1 s bridges a missed
// detection without dragging a stale point across a cut).
const rhythmContactHoldMs = 1000

// applyContactPoints returns the rhythm grid's per-frame search centre: the
// CSRT box centre (cx, cy), replaced by the nearest ContactPoint (within
// holdMs of the frame) only where that point lies further than the search
// radius (rhythmSearchCells cells) from the box. Inside the radius the grid
// reaches the contact on its own, so nothing changes there - that gate is
// what keeps the goldens unchanged (VLM1: clip_ausschnitt bit-identical,
// clip_voll same windowed r). With no points
// the input slices are returned as-is.
func applyContactPoints(cx, cy []float64, tsMs []int, pts []ContactPoint, holdMs int64,
	width, height, gw int) ([]float64, []float64) {
	if len(pts) == 0 || len(cx) == 0 || width <= 0 || height <= 0 || gw <= 0 {
		return cx, cy
	}
	if holdMs <= 0 {
		holdMs = rhythmContactHoldMs
	}
	sorted := append([]ContactPoint(nil), pts...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].Ms < sorted[j].Ms })
	radius := rhythmSearchCells * float64(width) / float64(gw)
	outX := append([]float64(nil), cx...)
	outY := append([]float64(nil), cy...)
	k := 0
	for i := range outX {
		if i >= len(tsMs) || i >= len(cy) {
			break
		}
		t := int64(tsMs[i])
		for k+1 < len(sorted) && absInt64(sorted[k+1].Ms-t) <= absInt64(sorted[k].Ms-t) {
			k++
		}
		p := sorted[k]
		if absInt64(p.Ms-t) > holdMs {
			continue
		}
		px, py := p.X*float64(width), p.Y*float64(height)
		if math.Hypot(px-cx[i], py-cy[i]) > radius {
			outX[i], outY[i] = px, py
		}
	}
	return outX, outY
}

// verifyContactPoints keeps the points the engine's own measurement backs
// (ContactVerifyK): per point, the window of m whose centre is nearest in
// time; the point stays only if the strongest cell in the 3x3 around it
// scores at least k times the window's chosen cell. Windows without a
// chosen cell or score map drop their points (nothing to verify against).
func verifyContactPoints(pts []ContactPoint, m SceneMap, k float64) []ContactPoint {
	return verifyContactPointsAt(pts, m, k, 0)
}

// verifyContactPointsAt is verifyContactPoints for a run that started
// offMs into the video (Options.StartTimeSec): the map's window times count
// from the first tracked frame, contact points are absolute video times.
func verifyContactPointsAt(pts []ContactPoint, m SceneMap, k float64, offMs int64) []ContactPoint {
	if len(pts) == 0 || len(m.Windows) == 0 || m.Cols <= 0 || m.Rows <= 0 {
		return nil
	}
	var out []ContactPoint
	for _, p := range pts {
		var w *MapWindow
		best := int64(-1)
		for i := range m.Windows {
			d := absInt64((m.Windows[i].StartMs+m.Windows[i].EndMs)/2 + offMs - p.Ms)
			if best < 0 || d < best {
				best, w = d, &m.Windows[i]
			}
		}
		if w.ChosenCell < 0 || w.ChosenCell >= len(w.Score) || len(w.Score) != m.Cols*m.Rows {
			continue
		}
		c := min(m.Cols-1, max(0, int(p.X*float64(m.Cols))))
		r := min(m.Rows-1, max(0, int(p.Y*float64(m.Rows))))
		at := 0
		for rr := max(0, r-1); rr <= min(m.Rows-1, r+1); rr++ {
			for cc := max(0, c-1); cc <= min(m.Cols-1, c+1); cc++ {
				at = max(at, int(w.Score[rr*m.Cols+cc]))
			}
		}
		if float64(at) >= k*float64(max(int(w.Score[w.ChosenCell]), 1)) {
			out = append(out, p)
		}
	}
	return out
}

func absInt64(v int64) int64 {
	if v < 0 {
		return -v
	}
	return v
}

// rhythmGridPositionsMarks is rhythmGridPositions with optional SceneMarks (M3).
func rhythmGridPositionsMarks(cellV [][]float32, gw, gh int, width, height int,
	cx, cy, anchor []float64, seed rhythmSeed, sceneCuts []int, fps float64,
	marks []SceneMark) []float64 {
	m := scoreWindows(cellV, gw, gh, width, height, cx, cy, fps)
	return chooseAndStitch(&m, cellV, gw, gh, width, height, cx, cy, anchor, seed, sceneCuts, fps, marks)
}

// rhythmGridPositionsWithMap is like rhythmGridPositions but also returns the
// SceneMap filled with ChosenCell / SignRule / TrackerR from the stitch step.
func rhythmGridPositionsWithMap(cellV [][]float32, gw, gh int, width, height int,
	cx, cy, anchor []float64, seed rhythmSeed, sceneCuts []int, fps float64) ([]float64, SceneMap) {
	return rhythmGridPositionsWithMapMarks(cellV, gw, gh, width, height, cx, cy, anchor, seed, sceneCuts, fps, nil)
}

// rhythmGridPositionsWithMapMarks is rhythmGridPositionsWithMap + SceneMarks.
func rhythmGridPositionsWithMapMarks(cellV [][]float32, gw, gh int, width, height int,
	cx, cy, anchor []float64, seed rhythmSeed, sceneCuts []int, fps float64,
	marks []SceneMark) ([]float64, SceneMap) {
	m := scoreWindows(cellV, gw, gh, width, height, cx, cy, fps)
	pos := chooseAndStitch(&m, cellV, gw, gh, width, height, cx, cy, anchor, seed, sceneCuts, fps, marks)
	for i := range m.Windows {
		m.Windows[i].scoreRaw = nil
		m.Windows[i].hasScore = false
	}
	return pos, m
}

func rhythmSegmentedSignReference(anchor []float64, sceneCuts []int, window int) []float64 {
	out := make([]float64, len(anchor))
	for start := 0; start < len(anchor); {
		_, end := rhythmSceneBounds(sceneCuts, start, len(anchor))
		if end <= start {
			end = len(anchor)
		}
		copy(out[start:end], subtractRollingMean(anchor[start:end], window))
		start = end
	}
	return out
}

func rhythmCellInSeed(px, py float64, seed rhythmSeed, cellW, cellH float64) bool {
	marginX := rhythmSeedMarginCells * cellW
	marginY := rhythmSeedMarginCells * cellH
	return px >= seed.X-marginX && px <= seed.X+seed.W+marginX &&
		py >= seed.Y-marginY && py <= seed.Y+seed.H+marginY
}

func rhythmSceneBounds(sceneCuts []int, frame, n int) (start, end int) {
	end = n
	for _, cut := range sceneCuts {
		if cut <= frame && cut > start {
			start = cut
		} else if cut > frame && cut < end {
			end = cut
		}
	}
	return start, end
}

// continuityR correlates cell's integrated motion over [a, end) with the
// curve already written there (v), so a new chunk keeps the orientation of
// the chunks before it.

func continuityR(cellV [][]float32, cell int, v []float64, a, end int) float64 {
	m := end - a
	cellPos, curve := make([]float64, m), make([]float64, m)
	var accC, accV float64
	for i := 0; i < m; i++ {
		accC += float64(cellV[a+i][cell])
		accV += v[a+i]
		cellPos[i], curve[i] = accC, accV
	}
	return pearson(cellPos, curve)
}

func rhythmScores(x [][]float64, fps float64) []float64 {
	score, _ := rhythmScoresWithTempo(x, fps)
	return score
}

func rhythmScoresWithTempo(x [][]float64, fps float64) (score []float64, tempoHz float64) {
	m := len(x[0])
	df := fps / float64(m)
	kLo, kHi := int(math.Ceil(rhythmTotalLoHz/df)), int(math.Floor(rhythmTotalHiHz/df))
	if kHi > m/2 {
		kHi = m / 2
	}
	if kHi <= kLo {
		return nil, 0
	}
	hann := make([]float64, m)
	for i := range hann {
		hann[i] = 0.5 - 0.5*math.Cos(2*math.Pi*float64(i)/float64(m-1))
	}
	nb := kHi - kLo + 1
	cosT, sinT := make([][]float64, nb), make([][]float64, nb)
	for j := 0; j < nb; j++ {
		k := float64(kLo + j)
		cosT[j], sinT[j] = make([]float64, m), make([]float64, m)
		for i := 0; i < m; i++ {
			ph := 2 * math.Pi * k * float64(i) / float64(m)
			cosT[j][i], sinT[j][i] = hann[i]*math.Cos(ph), hann[i]*math.Sin(ph)
		}
	}
	cells := len(x)
	power := make([][]float64, cells)
	for c := 0; c < cells; c++ {
		power[c] = make([]float64, nb)
		for j := 0; j < nb; j++ {
			var re, im float64
			for i, xv := range x[c] {
				re += xv * cosT[j][i]
				im += xv * sinT[j][i]
			}
			power[c][j] = re*re + im*im
		}
	}
	freq := func(j int) float64 { return float64(kLo+j) * df }
	inTempo := func(j int) bool { f := freq(j); return f >= rhythmTempoLoHz && f <= rhythmTempoHiHz }

	act := make([]float64, cells)
	for c := range act {
		for j := 0; j < nb; j++ {
			if inTempo(j) {
				act[c] += power[c][j]
			}
		}
	}
	top := topIndices(act, rhythmTopCells)
	f0, bestSum := 0.0, -1.0
	for j := 0; j < nb; j++ {
		if !inTempo(j) {
			continue
		}
		sum := 0.0
		for _, c := range top {
			sum += power[c][j]
		}
		if sum > bestSum {
			f0, bestSum = freq(j), sum
		}
	}
	if f0 == 0 {
		return nil, 0
	}

	score = make([]float64, cells)
	for c := 0; c < cells; c++ {
		var e, tot float64
		for j := 0; j < nb; j++ {
			f := freq(j)
			tot += power[c][j]
			if math.Abs(f-f0) <= rhythmBandHz || math.Abs(f-2*f0) <= rhythmBandHz {
				e += power[c][j]
			}
		}
		score[c] = e * e / (tot + 1e-9)
	}
	return score, f0
}

func detrendLinear(x []float64) {
	m := len(x)
	if m == 0 {
		return
	}
	mean := 0.0
	for _, v := range x {
		mean += v
	}
	mean /= float64(m)
	var num, den float64
	for i, v := range x {
		t := float64(i) - float64(m-1)/2
		num += t * (v - mean)
		den += t * t
	}
	slope := 0.0
	if den > 0 {
		slope = num / den
	}
	for i := range x {
		x[i] -= mean + slope*(float64(i)-float64(m-1)/2)
	}
}

func subtractRollingMean(x []float64, k int) []float64 {
	out := make([]float64, len(x))
	if k < 2 {
		copy(out, x)
		return out
	}
	prefix := make([]float64, len(x)+1)
	for i, v := range x {
		prefix[i+1] = prefix[i] + v
	}
	for i := range x {
		lo, hi := max(0, i-k/2), min(len(x), i-k/2+k)
		out[i] = x[i] - (prefix[hi]-prefix[lo])/float64(hi-lo)
	}
	return out
}

func pearson(a, b []float64) float64 {
	n := float64(len(a))
	if len(a) != len(b) || n < 2 {
		return 0
	}
	var ma, mb float64
	for i := range a {
		ma += a[i]
		mb += b[i]
	}
	ma, mb = ma/n, mb/n
	var sab, saa, sbb float64
	for i := range a {
		da, db := a[i]-ma, b[i]-mb
		sab += da * db
		saa += da * da
		sbb += db * db
	}
	if saa == 0 || sbb == 0 {
		return 0
	}
	return sab / math.Sqrt(saa*sbb)
}

func topIndices(v []float64, k int) []int {
	idx := make([]int, 0, k)
	for i := range v {
		if len(idx) < k {
			idx = append(idx, i)
			continue
		}
		minJ := 0
		for j := range idx {
			if v[idx[j]] < v[idx[minJ]] {
				minJ = j
			}
		}
		if v[i] > v[idx[minJ]] {
			idx[minJ] = i
		}
	}
	return idx
}
