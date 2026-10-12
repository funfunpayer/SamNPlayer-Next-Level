package funscript

import "testing"

func TestSuggestBackendAlwaysCSRT(t *testing.T) {
	for _, sz := range [][2]int{{18, 16}, {80, 90}, {0, 10}, {4, 4}} {
		if SuggestBackend(sz[0], sz[1]) != "csrt" {
			t.Fatalf("SuggestBackend(%d,%d) want csrt", sz[0], sz[1])
		}
	}
}
