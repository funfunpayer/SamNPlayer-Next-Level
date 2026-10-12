package generator

import (
	"testing"

	"github.com/funfunpayer/SamNPlayer/funscript"
)

func TestTruncateActionsMs(t *testing.T) {
	in := []funscript.Action{
		{At: 0, Pos: 0},
		{At: 30_000, Pos: 50},
		{At: 60_000, Pos: 100},
		{At: 90_000, Pos: 50},
	}
	out := truncateActionsMs(in, 60_000)
	if len(out) != 3 || out[len(out)-1].At != 60_000 {
		t.Fatalf("got %+v", out)
	}
	same := truncateActionsMs(in, 0)
	if len(same) != 4 {
		t.Fatalf("no-op cap: %d", len(same))
	}
}
