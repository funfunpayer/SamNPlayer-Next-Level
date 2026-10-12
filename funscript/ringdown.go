package funscript

// RingDown appends one or two damped half-cycles after atMs so a hold
// does not drop from a high position to 0 in one tick. It does not modify
// points before atMs. cycles=1 or 2; other values clamp to 1.
func RingDown(actions []Action, atMs int64, lastPos int, cycles int) []Action {
	if cycles < 1 {
		cycles = 1
	}
	if cycles > 2 {
		cycles = 2
	}
	out := append([]Action(nil), actions...)
	pos := lastPos
	if pos < 0 {
		pos = 0
	}
	if pos > 100 {
		pos = 100
	}
	step := int64(180)
	for i := 1; i <= cycles*2; i++ {
		amp := pos * (cycles*2 - i + 1) / (cycles*2 + 1)
		if i%2 == 1 {
			amp = amp / 3
		}
		out = append(out, Action{At: atMs + int64(i)*step, Pos: amp})
	}
	out = append(out, Action{At: atMs + int64(cycles*2+1)*step, Pos: 0})
	return out
}
